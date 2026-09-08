#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "AudioFileSourceLittleFS.h"
#include "AudioGeneratorWAV.h"

#include "AudioOutputI2S.h"

// Pin map
constexpr uint8_t PIN_SDA = 3;
constexpr uint8_t PIN_SCL = 4;
constexpr uint8_t PIN_I2S_LRC  = 5;
constexpr uint8_t PIN_I2S_BCLK = 6;
constexpr uint8_t PIN_I2S_DOUT = 7;
constexpr uint8_t PIN_EYES = 10;

// Tuning
constexpr const char *AUDIO_FILES[] = {"/clip1.wav", "/clip2.wav", "/clip3.wav"};
constexpr size_t AUDIO_FILE_COUNT = sizeof(AUDIO_FILES) / sizeof(AUDIO_FILES[0]);
float MOTION_THRESHOLD = 1.1f;
constexpr uint8_t MOTION_SAMPLES_REQUIRED = 3;
constexpr uint32_t SENSOR_INTERVAL_MS = 20;
constexpr uint32_t COOLDOWN_MS = 4000;
constexpr uint32_t EYE_FADE_IN_MS = 350;
constexpr uint32_t EYE_FADE_OUT_MS = 700;
constexpr uint8_t EYE_MAX_BRIGHTNESS = 255;
constexpr float AUDIO_GAIN = 1.0f;

Adafruit_MPU6050 mpu;
bool motionSensorAvailable = false;
AudioGeneratorWAV *wav = nullptr;
AudioFileSourceLittleFS *audioFile = nullptr;
AudioOutputI2S *audioOut = nullptr;
size_t nextAudioIndex = 0;

enum class RavenState { IDLE, EYES_UP, PLAYING, EYES_HOLD, EYES_DOWN, COOLDOWN };
RavenState state = RavenState::IDLE;

uint32_t stateStartedAt = 0;
uint32_t lastSensorReadAt = 0;
uint32_t eyeHoldDurationMs = 0;
float previousAx = 0, previousAy = 0, previousAz = 0;
float previousGx = 0, previousGy = 0, previousGz = 0;
bool havePreviousMotionSample = false;
uint8_t consecutiveMotionSamples = 0;

void setEyeBrightness(uint8_t brightness) {
  analogWrite(PIN_EYES, brightness);
}

void setState(RavenState newState) {
  state = newState;
  stateStartedAt = millis();
}

void stopAudio() {
  if (wav) {
    if (wav->isRunning()) wav->stop();
    delete wav;
    wav = nullptr;
  }
  if (audioFile) {
    delete audioFile;
    audioFile = nullptr;
  }
}

bool startAudio() {
  const char *audioPath = AUDIO_FILES[nextAudioIndex];
  // Advance once per activation, even if this clip is missing or invalid.
  nextAudioIndex = (nextAudioIndex + 1) % AUDIO_FILE_COUNT;
  if (!LittleFS.exists(audioPath)) {
    Serial.printf("ERROR: %s not found. Upload LittleFS.\n", audioPath);
    return false;
  }

  stopAudio();
  audioFile = new AudioFileSourceLittleFS(audioPath);
  wav = new AudioGeneratorWAV();

  if (!audioFile || !wav || !audioOut) {
    stopAudio();
    return false;
  }

  if (!wav->begin(audioFile, audioOut)) {
    Serial.println("ERROR: WAV playback failed to start.");
    stopAudio();
    return false;
  }

  Serial.printf("Playing %s\n", audioPath);
  return true;
}

float readMotionScore() {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  float ax = accel.acceleration.x;
  float ay = accel.acceleration.y;
  float az = accel.acceleration.z;
  float gx = gyro.gyro.x;
  float gy = gyro.gyro.y;
  float gz = gyro.gyro.z;

  if (!havePreviousMotionSample) {
    previousAx = ax; previousAy = ay; previousAz = az;
    previousGx = gx; previousGy = gy; previousGz = gz;
    havePreviousMotionSample = true;
    return 0.0f;
  }

  float accelDelta = fabsf(ax - previousAx) + fabsf(ay - previousAy) + fabsf(az - previousAz);
  float gyroDelta  = fabsf(gx - previousGx) + fabsf(gy - previousGy) + fabsf(gz - previousGz);

  previousAx = ax; previousAy = ay; previousAz = az;
  previousGx = gx; previousGy = gy; previousGz = gz;

  return accelDelta + (gyroDelta * 2.5f);
}

bool motionTriggered() {
  if (!motionSensorAvailable) return false;
  uint32_t now = millis();
  if (now - lastSensorReadAt < SENSOR_INTERVAL_MS) return false;
  lastSensorReadAt = now;

  float score = readMotionScore();
  // Uncomment while tuning:
  // Serial.printf("motion=%.3f\n", score);

  if (score >= MOTION_THRESHOLD) {
    if (consecutiveMotionSamples < 255) consecutiveMotionSamples++;
  } else {
    consecutiveMotionSamples = 0;
  }

  if (consecutiveMotionSamples >= MOTION_SAMPLES_REQUIRED) {
    consecutiveMotionSamples = 0;
    Serial.printf("Motion trigger! score=%.3f\n", score);
    return true;
  }
  return false;
}

uint8_t fadeValue(uint32_t elapsed, uint32_t duration, uint8_t from, uint8_t to) {
  if (elapsed >= duration) return to;
  float t = static_cast<float>(elapsed) / static_cast<float>(duration);
  float value = from + (to - from) * t;
  if (value < 0) return 0;
  if (value > 255) return 255;
  return static_cast<uint8_t>(value);
}

void updateRaven() {
  uint32_t now = millis();
  uint32_t elapsed = now - stateStartedAt;

  switch (state) {
    case RavenState::IDLE:
      setEyeBrightness(0);
      if (motionTriggered()) {
        Serial.println("Raven awakened.");
        setState(RavenState::EYES_UP);
      }
      break;

    case RavenState::EYES_UP:
      setEyeBrightness(fadeValue(elapsed, EYE_FADE_IN_MS, 0, EYE_MAX_BRIGHTNESS));
      if (elapsed >= EYE_FADE_IN_MS) {
        setEyeBrightness(EYE_MAX_BRIGHTNESS);
        eyeHoldDurationMs = EYE_FADE_IN_MS + EYE_FADE_OUT_MS;
        setState(startAudio() ? RavenState::PLAYING : RavenState::EYES_HOLD);
      }
      break;

    case RavenState::PLAYING:
      setEyeBrightness(EYE_MAX_BRIGHTNESS);
      if (wav && wav->isRunning()) {
        if (!wav->loop()) wav->stop();
      }
      if (!wav || !wav->isRunning()) {
        // Add the original fade + playback duration to double total LED on-time.
        eyeHoldDurationMs += millis() - stateStartedAt;
        stopAudio();
        setState(RavenState::EYES_HOLD);
      }
      break;

    case RavenState::EYES_HOLD:
      setEyeBrightness(EYE_MAX_BRIGHTNESS);
      if (elapsed >= eyeHoldDurationMs) {
        setState(RavenState::EYES_DOWN);
      }
      break;

    case RavenState::EYES_DOWN:
      setEyeBrightness(fadeValue(elapsed, EYE_FADE_OUT_MS, EYE_MAX_BRIGHTNESS, 0));
      if (elapsed >= EYE_FADE_OUT_MS) {
        setEyeBrightness(0);
        setState(RavenState::COOLDOWN);
        Serial.println("Cooldown.");
      }
      break;

    case RavenState::COOLDOWN:
      setEyeBrightness(0);
      if (motionSensorAvailable && now - lastSensorReadAt >= SENSOR_INTERVAL_MS) {
        lastSensorReadAt = now;
        readMotionScore();
      }
      if (elapsed >= COOLDOWN_MS) {
        consecutiveMotionSamples = 0;
        setState(RavenState::IDLE);
        Serial.println("Raven armed.");
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32-C3 Raven ===");

  pinMode(PIN_EYES, OUTPUT);
  setEyeBrightness(0);

  if (!LittleFS.begin(true)) {
    Serial.println("FATAL: LittleFS mount failed.");
    while (true) delay(1000);
  }

  Wire.begin(PIN_SDA, PIN_SCL);
  motionSensorAvailable = mpu.begin();
  if (motionSensorAvailable) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println("MPU6050 ready.");
  } else {
    Serial.println("MPU6050 not found. Bench mode: motion detection disabled.");
  }

  audioOut = new AudioOutputI2S();
  if (!audioOut) {
    Serial.println("FATAL: I2S output allocation failed.");
    while (true) delay(1000);
  }

  audioOut->SetPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
  audioOut->SetGain(AUDIO_GAIN);

  Serial.println("MAX98357A configured.");
  for (const char *audioPath : AUDIO_FILES) {
    Serial.printf("%s: %s\n", audioPath,
                  LittleFS.exists(audioPath) ? "found" : "WARNING: missing");
  }

  for (int b = 0; b <= 120; b += 5) { setEyeBrightness((uint8_t)b); delay(8); }
  for (int b = 120; b >= 0; b -= 5) { setEyeBrightness((uint8_t)b); delay(8); }

  setEyeBrightness(0);
  setState(RavenState::IDLE);
  Serial.println("Raven armed.");
  Serial.println("Send p to test the next clip and LEDs when armed.");
}

void loop() {
  while (Serial.available() > 0) {
    int command = Serial.read();
    if (command == 'p' || command == 'P') {
      if (state == RavenState::IDLE) {
        Serial.println("Manual playback trigger.");
        setState(RavenState::EYES_UP);
      } else {
        Serial.println("Raven busy. Wait for Raven armed, then send p again.");
      }
    }
  }
  updateRaven();
  delay(1);
}
