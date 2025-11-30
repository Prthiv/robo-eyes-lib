/*
  RideBuddyEyes.cpp - A procedural library for cute, expressive robot eyes.
  Created by Gemini, 2025.
  Rewritten for cuteness and simplicity, now with smooth animations.
  Released into the public domain.
*/

#include "RideBuddyEyes.h"
#include <math.h>

RideBuddyEyes::RideBuddyEyes() {
  _display = nullptr;
  _currentEmotion = NEUTRAL;
  _previousEmotion = NEUTRAL;
  _animDuration = 0;
  _isIdleActionActive = false;
  _patMoveDirection = false;
  _vibrateXOffset = 0;
  _vibrateYOffset = 0;
}

void RideBuddyEyes::begin(Adafruit_SH110X* display) {
  _display = display;
  for (int i = 0; i < 2; i++) {
    _eyeCenterX[i] = (SCREEN_WIDTH / 4) * (i == 0 ? 1 : 3);
    _eyeCenterY[i] = SCREEN_HEIGHT / 2;
    // Initial state set to NEUTRAL base
    _currentState[i] = {0, 0, EYE_WIDTH, EYE_HEIGHT};
    _startState[i] = _currentState[i];
    _targetState[i] = _currentState[i];
  }
  unsigned long currentTime = millis();
  _lastFrameTime = currentTime;
  _nextBlinkTime = currentTime + random(AUTO_BLINK_MIN_INTERVAL, AUTO_BLINK_MAX_INTERVAL);
  _nextScaredMoveTime = 0;
  _nextIdleActionTime = currentTime + random(IDLE_ACTION_MIN_INTERVAL, IDLE_ACTION_MAX_INTERVAL);
  _idleActionEndTime = 0;
  _nextVibrateTime = 0;
  _nextLoveMoveTime = 0;
}

void RideBuddyEyes::update() {
  unsigned long currentTime = millis();
  if (currentTime - _lastFrameTime < FRAME_INTERVAL) return;
  _lastFrameTime = currentTime;

  updateAnimation();

  // --- Handle automatic blinking ---
  // Allow blinking during DRIVING to make the driving emotion less intense/robotic.
  if (_currentEmotion != BLINK && _currentEmotion != SCARED && !_isIdleActionActive && currentTime >= _nextBlinkTime) {
    _blinkStartTime = currentTime;
    setEmotion(BLINK);
    uint16_t nextBlinkInterval = random(AUTO_BLINK_MIN_INTERVAL, AUTO_BLINK_MAX_INTERVAL);
    _nextBlinkTime = currentTime + (_previousEmotion == SAD ? SAD_BLINK_DURATION : BLINK_DURATION) + nextBlinkInterval;
  }
  uint16_t currentBlinkDuration = (_previousEmotion == SAD) ? SAD_BLINK_DURATION : BLINK_DURATION;
  if (_currentEmotion == BLINK && (currentTime - _blinkStartTime > currentBlinkDuration)) {
    setEmotion(_previousEmotion);
  }

  if (_currentEmotion == PAT && currentTime > _patEndTime) {
    setEmotion(_previousEmotion);
    // On exiting PAT, smoothly return eyes to center.
    EyeState neutralState = {0, 0, EYE_WIDTH, EYE_HEIGHT};
    startAnimation(neutralState, PAT_MOVE_MIN_INTERVAL);
  }

  // --- Handle Per-Emotion Behaviors ---
  if (_currentEmotion == PAT && currentTime >= _nextPatMoveTime) {
    _patMoveDirection = !_patMoveDirection; // Toggle direction
    EyeState patTarget = {(_patMoveDirection ? (float)PAT_MOVE_MAGNITUDE : (float)-PAT_MOVE_MAGNITUDE), 0, EYE_WIDTH, EYE_HEIGHT};
    startAnimation(patTarget, PAT_MOVE_MIN_INTERVAL - 50);
    _nextPatMoveTime = currentTime + random(PAT_MOVE_MIN_INTERVAL, PAT_MOVE_MAX_INTERVAL);
  }
  
  if (_currentEmotion == SCARED && currentTime >= _nextScaredMoveTime) {
    EyeState scaredTarget = {(float)random(-5, 6), (float)random(-3, 4), SCARED_EYE_WIDTH, SCARED_EYE_HEIGHT};
    startAnimation(scaredTarget, SCARED_MOVE_MIN_INTERVAL - 50);
    _nextScaredMoveTime = currentTime + random(SCARED_MOVE_MIN_INTERVAL, SCARED_MOVE_MAX_INTERVAL);
  }
  else if (_currentEmotion == LOVE && currentTime >= _nextLoveMoveTime) {
    EyeState loveTarget = {(float)random(-6, 7), (float)random(-4, 5), EYE_WIDTH, EYE_HEIGHT};
    startAnimation(loveTarget, LOVE_MOVE_MIN_INTERVAL - 100);
    _nextLoveMoveTime = currentTime + random(LOVE_MOVE_MIN_INTERVAL, LOVE_MOVE_MAX_INTERVAL);
  }
  else if (_currentEmotion == NEUTRAL && !_isIdleActionActive && currentTime >= _nextIdleActionTime) {
    _isIdleActionActive = true;
    EyeState idleTarget = {(float)random(-8, 9), (float)random(-6, 7), EYE_WIDTH, EYE_HEIGHT};
    startAnimation(idleTarget, IDLE_ACTION_TRANSITION_DURATION);
    _idleActionEndTime = currentTime + IDLE_ACTION_TRANSITION_DURATION + IDLE_ACTION_HOLD_DURATION;
  }
  
  if (_isIdleActionActive && currentTime >= _idleActionEndTime) {
    _isIdleActionActive = false;
    EyeState neutralState = {0, 0, EYE_WIDTH, EYE_HEIGHT};
    startAnimation(neutralState, IDLE_ACTION_TRANSITION_DURATION);
    _nextIdleActionTime = currentTime + IDLE_ACTION_TRANSITION_DURATION + random(IDLE_ACTION_MIN_INTERVAL, IDLE_ACTION_MAX_INTERVAL);
  }

  if ((_currentEmotion == SAD || _currentEmotion == ANGRY) && currentTime >= _nextVibrateTime) {
      _vibrateXOffset = random(-VIBRATE_MAGNITUDE, VIBRATE_MAGNITUDE + 1);
      _vibrateYOffset = random(-VIBRATE_MAGNITUDE, VIBRATE_MAGNITUDE + 1);
      _nextVibrateTime = currentTime + VIBRATE_INTERVAL;
  } else if (!(_currentEmotion == SAD || _currentEmotion == ANGRY)) {
      _vibrateXOffset = 0;
      _vibrateYOffset = 0;
  }

  drawEyes();
}

void RideBuddyEyes::setEmotion(Emotion emotion) {
  if (_currentEmotion == emotion && emotion != BLINK) return;
  if (_currentEmotion != BLINK) _previousEmotion = _currentEmotion;
  
  _currentEmotion = emotion;
  _isIdleActionActive = false; // Stop any idle action when emotion changes

  // Determine the target base state for the new emotion
  EyeState newBaseState = {0, 0, EYE_WIDTH, EYE_HEIGHT}; // Default to normal size
  if (emotion == SCARED) {
    newBaseState = {0, 0, SCARED_EYE_WIDTH, SCARED_EYE_HEIGHT};
  }

  // Start a smooth transition to the new emotion's base size/position
  if (emotion != BLINK) { // Don't animate base state if just blinking
    startAnimation(newBaseState, EMOTION_TRANSITION_DURATION);
  }

  // Reset any specific behavior state when changing away from relevant emotions
  if (_previousEmotion == SAD && emotion != SAD) { _vibrateXOffset = 0; _vibrateYOffset = 0; }
  if (_previousEmotion == ANGRY && emotion != ANGRY) { _vibrateXOffset = 0; _vibrateYOffset = 0; }
  
  // Specific initializations for new emotion behaviors
  if (emotion == LOVE) {
    _nextLoveMoveTime = millis() + 100;
  } else if (emotion == SCARED) {
    _nextScaredMoveTime = millis() + 100;
  } else if (emotion == NEUTRAL) {
    _nextIdleActionTime = millis() + random(IDLE_ACTION_MIN_INTERVAL, IDLE_ACTION_MAX_INTERVAL);
  } else if (emotion == PAT) {
    if (_currentEmotion == PAT) return; // Don't restart if already patting
    _patEndTime = millis() + 2000; // Show >< for 2 seconds
    _nextPatMoveTime = millis();
  }
}

// --- Public API & Animation System ---
void RideBuddyEyes::neutral()   { setEmotion(NEUTRAL); }
void RideBuddyEyes::sad()       { setEmotion(SAD); }
void RideBuddyEyes::angry()     { setEmotion(ANGRY); }
void RideBuddyEyes::love()      { setEmotion(LOVE); }
void RideBuddyEyes::scared()    { setEmotion(SCARED); }
void RideBuddyEyes::pat()       { setEmotion(PAT); }
void RideBuddyEyes::serious()   { setEmotion(SERIOUS); }
void RideBuddyEyes::happy()     { setEmotion(HAPPY); }
void RideBuddyEyes::blink()     { setEmotion(BLINK); }

void RideBuddyEyes::startAnimation(const EyeState& target, uint16_t duration) {
  _animStartTime = millis();
  _animDuration = duration;
  for (int i = 0; i < 2; i++) {
    _startState[i] = _currentState[i];
    _targetState[i] = target;
  }
}

float RideBuddyEyes::easeInOut(float t) { return t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t; }

void RideBuddyEyes::updateAnimation() {
  if (_animDuration == 0) return;
  unsigned long elapsed = millis() - _animStartTime;
  float progress = (float)elapsed / _animDuration;
  if (progress >= 1.0) { progress = 1.0; _animDuration = 0; }
  float easedProgress = easeInOut(progress);
  for (int i = 0; i < 2; i++) {
    _currentState[i].xOffset = _startState[i].xOffset + (_targetState[i].xOffset - _startState[i].xOffset) * easedProgress;
    _currentState[i].yOffset = _startState[i].yOffset + (_targetState[i].yOffset - _startState[i].yOffset) * easedProgress;
    _currentState[i].width = _startState[i].width + (_targetState[i].width - _startState[i].width) * easedProgress;
    _currentState[i].height = _startState[i].height + (_targetState[i].height - _startState[i].height) * easedProgress;
  }
}


// --- Drawing ---
void RideBuddyEyes::drawEyes() {
  if (!_display) return;
  _display->clearDisplay();

  // Draw eyes first
  for (uint8_t i = 0; i < 2; i++) {
    drawOneEye(i, _currentEmotion);
  }

  _display->display();
}


void RideBuddyEyes::drawOneEye(uint8_t i, Emotion emotion) {
  int16_t x = _eyeCenterX[i] + _currentState[i].xOffset + _vibrateXOffset;
  int16_t y = _eyeCenterY[i] + _currentState[i].yOffset + _vibrateYOffset;
  int16_t w = _currentState[i].width;
  int16_t h = _currentState[i].height;

  // Blinking eyes are always just a line, overrides other emotions
  if (emotion == BLINK) {
      _display->drawFastHLine(x - EYE_WIDTH / 2, y, EYE_WIDTH, SH110X_WHITE);
      _display->drawFastHLine(x - EYE_WIDTH / 2, y-1, EYE_WIDTH, SH110X_WHITE);
      return;
  }

  switch(emotion) {
    case NEUTRAL:
      _display->fillRoundRect(x - w/2, y - h/2, w, h, EYE_CORNER_RADIUS, SH110X_WHITE);
      break;
    
    case SAD:
      _display->fillRoundRect(x - w/2, y - h/2, w, h, EYE_CORNER_RADIUS, SH110X_WHITE);
      if (i == 0) _display->fillTriangle(x - w/2 - 1, y - h/2 - 1, x + w/2, y - h/2 - 1, x - w/2 - 1, y, SH110X_BLACK);
      else _display->fillTriangle(x + w/2 + 1, y - h/2 - 1, x - w/2, y - h/2 - 1, x + w/2 + 1, y, SH110X_BLACK);
      break;

    case ANGRY:
      _display->fillRoundRect(x - w/2, y - h/2, w, h, EYE_CORNER_RADIUS, SH110X_WHITE);
      if (i == 0) _display->fillTriangle(x + w/2 + 1, y - h/2 - 1, x - w/2, y - h/2 - 1, x + w/2 + 1, y, SH110X_BLACK);
      else _display->fillTriangle(x - w/2 - 1, y - h/2 - 1, x + w/2, y - h/2 - 1, x - w/2 - 1, y, SH110X_BLACK);
      break;

    case LOVE: {
      float scale = 1.0 + (sin(millis() * HEARTBEAT_SPEED) * HEARTBEAT_MAGNITUDE);
      int16_t heartW = w * scale;
      int16_t heartH = h * scale;
      int16_t heartSize = heartW / 2;
      int16_t heartY = y - 5;
      
      _display->fillCircle(x - heartSize/2, heartY, heartSize/2, SH110X_WHITE);
      _display->fillCircle(x + heartSize/2, heartY, heartSize/2, SH110X_WHITE);
      _display->fillTriangle(
        x - heartSize, heartY,
        x + heartSize, heartY,
        x,             heartY + heartSize,
        SH110X_WHITE
      );
      break;
    }

    case PAT: {
      int16_t w = 24;
      int16_t h = 24;
      if (i == 0) { // Left eye >
          _display->drawLine(x - w/2, y - h/2, x + w/2, y, SH110X_WHITE);
          _display->drawLine(x - w/2, y + h/2, x + w/2, y, SH110X_WHITE);
          _display->drawLine(x - w/2, y - h/2 + 1, x + w/2, y, SH110X_WHITE);
          _display->drawLine(x - w/2, y + h/2 - 1, x + w/2, y, SH110X_WHITE);
          _display->drawLine(x - w/2, y - h/2 + 2, x + w/2, y, SH110X_WHITE); // Even thicker
          _display->drawLine(x - w/2, y + h/2 - 2, x + w/2, y, SH110X_WHITE); // Even thicker
      } else { // Right eye <
          _display->drawLine(x + w/2, y - h/2, x - w/2, y, SH110X_WHITE);
          _display->drawLine(x + w/2, y + h/2, x - w/2, y, SH110X_WHITE);
          _display->drawLine(x + w/2, y - h/2 + 1, x - w/2, y, SH110X_WHITE);
          _display->drawLine(x + w/2, y + h/2 - 1, x - w/2, y, SH110X_WHITE);
          _display->drawLine(x + w/2, y - h/2 + 2, x - w/2, y, SH110X_WHITE); // Even thicker
          _display->drawLine(x + w/2, y + h/2 - 2, x - w/2, y, SH110X_WHITE); // Even thicker
      }
      break;
    }

    case SCARED:
      _display->fillRoundRect(x - w/2, y - h/2, w, h, h/2, SH110X_WHITE);
      break;

    case SERIOUS:
      // Draw the bottom half of a round rect to make a 'u' shape
      _display->fillRoundRect(x - w/2, y - h/2, w, h, EYE_CORNER_RADIUS, SH110X_WHITE);
      _display->fillRect(x - w/2, y - h/2, w, h/2 + 2, SH110X_BLACK);
      break;

    case HAPPY: {
      int16_t w = EYE_WIDTH / 2; // Smaller
      int16_t h = EYE_HEIGHT / 2; // Smaller
      // Draw the ^ shape
      _display->drawLine(x - w/2, y + h/2, x, y - h/2, SH110X_WHITE);
      _display->drawLine(x + w/2, y + h/2, x, y - h/2, SH110X_WHITE);
      // Make it thicker
      _display->drawLine(x - w/2 + 1, y + h/2, x + 1, y - h/2, SH110X_WHITE);
      _display->drawLine(x + w/2 - 1, y + h/2, x - 1, y - h/2, SH110X_WHITE);
      _display->drawLine(x - w/2 + 2, y + h/2, x + 2, y - h/2, SH110X_WHITE);
      _display->drawLine(x + w/2 - 2, y + h/2, x - 2, y - h/2, SH110X_WHITE);
      _display->drawLine(x - w/2 + 3, y + h/2, x + 3, y - h/2, SH110X_WHITE);
      _display->drawLine(x + w/2 - 3, y + h/2, x - 3, y - h/2, SH110X_WHITE);
      break;
    }
  }
}