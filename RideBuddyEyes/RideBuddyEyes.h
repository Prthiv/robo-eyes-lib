/*
  RideBuddyEyes.h - A procedural library for cute, expressive robot eyes.
  Created by Gemini, 2025.
  Released into the public domain.
*/
#ifndef RideBuddyEyes_h
#define RideBuddyEyes_h

#include "Arduino.h"
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// --- Core Definitions ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// --- Hardware Pins ---
#define TOUCH_PIN 27

// --- Eye Geometry ---
#define EYE_WIDTH 38
#define EYE_HEIGHT 42
#define EYE_CORNER_RADIUS 12

// --- Animation Timings & Parameters ---
#define FRAME_INTERVAL 50 // 20 FPS
#define BLINK_DURATION 150 
#define AUTO_BLINK_MIN_INTERVAL 2500
#define AUTO_BLINK_MAX_INTERVAL 7000
#define IDLE_ACTION_MIN_INTERVAL 3000
#define IDLE_ACTION_MAX_INTERVAL 6000
#define IDLE_ACTION_HOLD_DURATION 1000
#define IDLE_ACTION_TRANSITION_DURATION 400

// --- Emotion Enum ---
enum Emotion {
  NEUTRAL,
  BLINK,
  HAPPY,
  CRY,
  LOVE,
  SHY,
  ANGRY,
  DRIVING,
  SCARED,
  DISTRACTED,
  SLEEP,
  BATTERY
};

// --- Distracted Emotion Phases ---
enum DistractedPhase {
  DISTRACTED_INIT,
  DISTRACTED_LEFT_ANIM,
  DISTRACTED_LEFT_HOLD,
  DISTRACTED_RETURN_TO_NEUTRAL_FROM_LEFT,
  DISTRACTED_NEUTRAL_HOLD_BETWEEN_SIDES,
  DISTRACTED_RIGHT_ANIM,
  DISTRACTED_RIGHT_HOLD,
  DISTRACTED_RETURN_TO_NEUTRAL_FROM_RIGHT,
  DISTRACTED_NEUTRAL_WAIT_BEFORE_FIRST_ANIM
};

// --- Data Structures ---
struct EyeState {
  float xOffset;
  float yOffset;
  float width;
  float height;
};

class RideBuddyEyes {
  public:
    RideBuddyEyes();
    void begin(U8G2* display, int sda_pin, int scl_pin);
    void update();
    
    // --- Emotion API ---
    void setEmotion(Emotion emotion);
    void neutral();
    void blink();
    void happy();
    void cry();
    void love();
    void shy();
    void angry();
    void driving();
    void distracted();
    void sleep();
    void scared();
    void battery();

  private:
    U8G2* _display;
    Adafruit_MPU6050 _mpu;
    int16_t _eyeCenterX[2];
    int16_t _eyeCenterY[2];
    Emotion _currentEmotion;
    Emotion _previousEmotion;

    unsigned long _lastFrameTime;

    // --- State Variables ---
    unsigned long _nextBlinkTime;
    unsigned long _blinkStartTime;
    unsigned long _nextIdleActionTime;
    unsigned long _idleActionEndTime;
    bool _isIdleActionActive;
    int8_t _vibrateXOffset;
    int8_t _vibrateYOffset;

    // --- Cry Animation State ---
    int _cry_anim_currentFrame;
    unsigned long _cry_anim_lastFrameTime;
    int _shy_anim_currentFrame;
    unsigned long _shy_anim_lastFrameTime;
    int _driving_anim_currentFrame;
    unsigned long _driving_anim_lastFrameTime;
    // --- Happy Animation State ---
    int _happy_anim_currentFrame;
    unsigned long _happy_anim_lastFrameTime;
    // --- Battery Animation State ---
    int _battery_anim_currentFrame;
    unsigned long _battery_anim_lastFrameTime;

    // --- Distracted Animation State ---
    DistractedPhase _distractedPhase;
    unsigned long _distractedPhaseStartTime;

    // --- Sleep Animation State ---
    enum SleepMouthState { SLEEP_MOUTH_UNSHAPED, SLEEP_MOUTH_OVAL };
    SleepMouthState _sleepMouthState;
    unsigned long _sleepMouthLastToggleTime;



    // --- Animation System State ---
    EyeState _currentState[2];
    EyeState _startState[2];
    EyeState _targetState[2];
    unsigned long _animStartTime;
    uint16_t _animDuration;

    // --- Private Methods ---
    void updateAnimation();
    void startAnimation(const EyeState& target, uint16_t duration);
    void startAnimation(const EyeState& targetLeft, const EyeState& targetRight, uint16_t duration);
    float easeInOut(float t);
    void drawEyes();
    void drawOneEye(uint8_t i, Emotion emotion);
    void drawHeart(int x, int y, int size);
    void drawSleepZzz(int eye_x, int eye_y, int eye_w, int eye_h);
    void drawMouth(int x, int y, int w, int h);
};

#endif