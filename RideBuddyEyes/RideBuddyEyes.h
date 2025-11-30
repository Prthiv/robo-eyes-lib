/*
  RideBuddyEyes.h - A procedural library for cute, expressive robot eyes.
  Created by Gemini, 2025.
  Rewritten for cuteness and simplicity, now with smooth animations.
  Released into the public domain.
*/
#ifndef RideBuddyEyes_h
#define RideBuddyEyes_h

#include "Arduino.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// --- Core Definitions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// --- Eye Geometry
#define EYE_WIDTH 38
#define EYE_HEIGHT 42
#define EYE_CORNER_RADIUS 12
#define SCARED_EYE_WIDTH 28
#define SCARED_EYE_HEIGHT 28

// --- Animation Timings & Parameters
#define FRAME_INTERVAL 16 // Target ~60 FPS
#define BLINK_DURATION 150 
#define SAD_BLINK_DURATION 1000
#define AUTO_BLINK_MIN_INTERVAL 2500
#define AUTO_BLINK_MAX_INTERVAL 7000

#define SCARED_MOVE_MIN_INTERVAL 200
#define SCARED_MOVE_MAX_INTERVAL 400
#define EMOTION_TRANSITION_DURATION 600

#define IDLE_ACTION_MIN_INTERVAL 3000
#define IDLE_ACTION_MAX_INTERVAL 6000
#define IDLE_ACTION_HOLD_DURATION 1000
#define IDLE_ACTION_TRANSITION_DURATION 400

#define VIBRATE_INTERVAL 50
#define VIBRATE_MAGNITUDE 1

#define LOVE_MOVE_MIN_INTERVAL 800
#define LOVE_MOVE_MAX_INTERVAL 1500
#define HEARTBEAT_SPEED 0.005
#define HEARTBEAT_MAGNITUDE 0.15

#define STEERING_SPEED 0.0015
#define STEERING_MAGNITUDE 2

#define PAT_MOVE_MIN_INTERVAL 150
#define PAT_MOVE_MAX_INTERVAL 300
#define PAT_MOVE_MAGNITUDE 10

// --- Emotion Enum
enum Emotion {
  NEUTRAL,
  SAD,
  ANGRY,
  LOVE,
  SCARED,
  BLINK,
  PAT,
  SERIOUS,
  HAPPY
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
    void begin(Adafruit_SH110X* display);
    void update();
    
    // --- Emotion API ---
    void setEmotion(Emotion emotion);
    void neutral();
    void sad();
    void angry();
    void love();
    void scared();
    void pat();
    void serious();
    void happy();
  // driving() removed — DRIVING emotion removed
    void blink();

  private:
    Adafruit_SH110X* _display;
    int16_t _eyeCenterX[2];
    int16_t _eyeCenterY[2];
    Emotion _currentEmotion;
    Emotion _previousEmotion;

    unsigned long _lastFrameTime;

    // --- State Variables ---
    unsigned long _nextBlinkTime;
    unsigned long _blinkStartTime;
    unsigned long _patEndTime;
    unsigned long _nextScaredMoveTime;
    unsigned long _nextIdleActionTime;
    unsigned long _idleActionEndTime;
    bool _isIdleActionActive;
    bool _patMoveDirection;
    unsigned long _nextPatMoveTime;
    int8_t _vibrateXOffset;
    int8_t _vibrateYOffset;
    unsigned long _nextVibrateTime;
    unsigned long _nextLoveMoveTime;

    // --- Animation System State ---
    EyeState _currentState[2];
    EyeState _startState[2];
    EyeState _targetState[2];
    unsigned long _animStartTime;
    uint16_t _animDuration;

    // --- Private Methods ---
    void updateAnimation();
    void startAnimation(const EyeState& target, uint16_t duration);
    float easeInOut(float t);
    
    void drawEyes();
    void drawOneEye(uint8_t i, Emotion emotion);
};

#endif