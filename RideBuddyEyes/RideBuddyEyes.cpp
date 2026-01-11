#include "RideBuddyEyes.h"
#include <math.h>
#include "data/cry.h"
#include "data/shy.h"
#include "data/angry.h"
#include "data/driving.h"
#include "data/happy.h" // Include happy animation data
#include "data/love.h" // Include love bitmap data
#include "data/battery.h" // Include battery animation data

// Note: The extern declarations were removed as they conflicted with the definitions in the header files.
// The header files provide the correct declarations for frame data arrays and frame counts.

#define CRY_FRAME_DURATION 33 // ~30 FPS
#define HAPPY_FRAME_DURATION 33 // ~30 FPS
#define BATTERY_FRAME_DURATION 33 // ~30 FPS
#define LOVE_BEAT_SPEED 0.005
#define LOVE_BEAT_MAGNITUDE 0.2
#define SHY_FRAME_DURATION 33 // ~30 FPS
#define DRIVING_FRAME_DURATION 33 // ~30 FPS
#define DISTRACTED_ANIM_DURATION 300 // Duration for eye scaling/moving
#define DISTRACTED_SIDE_HOLD_DURATION 2000 // 2 seconds holding the side-distracted state
#define DISTRACTED_NEUTRAL_HOLD_DURATION 3000 // 3 seconds holding neutral between sides
#define SLEEP_MOUTH_TOGGLE_INTERVAL 500 // Interval to toggle sleep mouth shape (ms)

RideBuddyEyes::RideBuddyEyes() {
  _display = nullptr;
  _currentEmotion = NEUTRAL;
  _previousEmotion = NEUTRAL;
  _animDuration = 0;
  _vibrateXOffset = 0;
  _vibrateYOffset = 0;
  _isIdleActionActive = false;
  _cry_anim_currentFrame = 0;
  _shy_anim_currentFrame = 0;
  _shy_anim_lastFrameTime = 0;
  _driving_anim_currentFrame = 0;
  _driving_anim_lastFrameTime = 0;
  _happy_anim_currentFrame = 0;       // Initialize happy animation current frame
  _happy_anim_lastFrameTime = 0;      // Initialize happy animation last frame time
  _battery_anim_currentFrame = 0;     // Initialize battery animation current frame
  _battery_anim_lastFrameTime = 0;    // Initialize battery animation last frame time
  _distractedPhase = DISTRACTED_INIT;
  _distractedPhaseStartTime = 0;
  _sleepMouthState = SLEEP_MOUTH_UNSHAPED;
  _sleepMouthLastToggleTime = 0;

}

void RideBuddyEyes::begin(U8G2* display, int sda_pin, int scl_pin) {
  _display = display;
  Wire.begin(sda_pin, scl_pin);
  pinMode(TOUCH_PIN, INPUT_PULLUP);

  for (int i = 0; i < 2; i++) {
    _eyeCenterX[i] = (SCREEN_WIDTH / 4) * (i == 0 ? 1 : 3);
    _eyeCenterY[i] = SCREEN_HEIGHT / 2 - 8;
    _currentState[i] = {0, 0, EYE_WIDTH, EYE_HEIGHT};
    _startState[i] = _currentState[i];
    _targetState[i] = _currentState[i];
  }

  unsigned long currentTime = millis();
  _lastFrameTime = currentTime;
  _nextBlinkTime = currentTime + random(AUTO_BLINK_MIN_INTERVAL, AUTO_BLINK_MAX_INTERVAL);
  _nextIdleActionTime = currentTime + random(IDLE_ACTION_MIN_INTERVAL, IDLE_ACTION_MAX_INTERVAL);
  _idleActionEndTime = 0;
}

void RideBuddyEyes::update() {
  unsigned long currentTime = millis();
  if (currentTime - _lastFrameTime < FRAME_INTERVAL) return;
  _lastFrameTime = currentTime;

  // Always update animation for procedural eyes
  if (_currentEmotion == NEUTRAL || 
      _currentEmotion == BLINK || _currentEmotion == DISTRACTED || _currentEmotion == SLEEP || _currentEmotion == SCARED) {
      updateAnimation();
  }

  // Handle blinking and idle actions (only for procedural eyes)
  if (_currentEmotion != CRY && _currentEmotion != SHY && _currentEmotion != DRIVING && _currentEmotion != HAPPY && _currentEmotion != BATTERY && _currentEmotion != DISTRACTED) { // Exclude bitmap emotions
      if (_currentEmotion != BLINK && !_isIdleActionActive && currentTime >= _nextBlinkTime) {
        _blinkStartTime = currentTime;
        setEmotion(BLINK);
        _nextBlinkTime = currentTime + BLINK_DURATION + random(AUTO_BLINK_MIN_INTERVAL, AUTO_BLINK_MAX_INTERVAL);
      }
      if (_currentEmotion == BLINK && (currentTime - _blinkStartTime > BLINK_DURATION)) {
        setEmotion(_previousEmotion);
      }
      if (_currentEmotion == NEUTRAL) {
        _vibrateXOffset = 0; _vibrateYOffset = 0;
        if (!_isIdleActionActive && currentTime >= _nextIdleActionTime) {
          _isIdleActionActive = true;
          EyeState glanceTarget = {(float)random(-8, 9), (float)random(-6, 7), EYE_WIDTH, EYE_HEIGHT};
          startAnimation(glanceTarget, IDLE_ACTION_TRANSITION_DURATION);
          _idleActionEndTime = currentTime + IDLE_ACTION_TRANSITION_DURATION + IDLE_ACTION_HOLD_DURATION;
        }
        if (_isIdleActionActive && currentTime >= _idleActionEndTime) {
          _isIdleActionActive = false;
          EyeState centerTarget = {0, 0, EYE_WIDTH, EYE_HEIGHT};
          startAnimation(centerTarget, IDLE_ACTION_TRANSITION_DURATION);
          _nextIdleActionTime = currentTime + random(IDLE_ACTION_MIN_INTERVAL, IDLE_ACTION_MAX_INTERVAL);
        }
      } else if (_currentEmotion == ANGRY) {
        _vibrateXOffset = random(-2, 3);
        _vibrateYOffset = random(-2, 3);
      } else if (_currentEmotion == LOVE) {
        _vibrateXOffset = random(-1, 2);
        _vibrateYOffset = random(-1, 2);
      } else { // For other static emotions without special vibration (BLINK, DISTRACTED, SLEEP, SCARED)
        _vibrateXOffset = 0; _vibrateYOffset = 0; // Reset vibration for these
      }
  } // End of blinking and idle actions block

  // Handle specific animation phase logic (DISTRACTED, SLEEP, SCARED)
  if (_currentEmotion == DISTRACTED) {
      // DISTRACTED logic (Restored)
      switch (_distractedPhase) {
        case DISTRACTED_INIT: {
          // Start the first animation (looking left)
          EyeState targetLeftEye = {-15, 0, EYE_WIDTH * 1.5, EYE_HEIGHT * 1.5};
          EyeState targetRightEye = {-10, 0, EYE_WIDTH * 0.5, EYE_HEIGHT * 0.5};
          startAnimation(targetLeftEye, targetRightEye, DISTRACTED_ANIM_DURATION);
          _distractedPhase = DISTRACTED_LEFT_ANIM;
          _distractedPhaseStartTime = currentTime;
          break;
        }
        case DISTRACTED_NEUTRAL_WAIT_BEFORE_FIRST_ANIM: {
          if (_animDuration == 0) {
            EyeState targetLeftEye = {-15, 0, EYE_WIDTH * 1.5, EYE_HEIGHT * 1.5};
            EyeState targetRightEye = {-10, 0, EYE_WIDTH * 0.5, EYE_HEIGHT * 0.5};
            startAnimation(targetLeftEye, targetRightEye, DISTRACTED_ANIM_DURATION);
            _distractedPhase = DISTRACTED_LEFT_ANIM;
            _distractedPhaseStartTime = currentTime;
          }
          break;
        }
        case DISTRACTED_LEFT_ANIM: {
          if (_animDuration == 0) {
            _distractedPhase = DISTRACTED_LEFT_HOLD;
            _distractedPhaseStartTime = currentTime;
          }
          break;
        }
        case DISTRACTED_LEFT_HOLD: {
          if (currentTime - _distractedPhaseStartTime >= DISTRACTED_SIDE_HOLD_DURATION) {
            EyeState neutralTarget = {0, 0, EYE_WIDTH, EYE_HEIGHT};
            startAnimation(neutralTarget, DISTRACTED_ANIM_DURATION);
            _distractedPhase = DISTRACTED_RETURN_TO_NEUTRAL_FROM_LEFT;
            _distractedPhaseStartTime = currentTime;
          }
          break;
        }
        case DISTRACTED_RETURN_TO_NEUTRAL_FROM_LEFT: {
          if (_animDuration == 0) {
            _distractedPhase = DISTRACTED_NEUTRAL_HOLD_BETWEEN_SIDES;
            _distractedPhaseStartTime = currentTime;
          }
          break;
        }
        case DISTRACTED_NEUTRAL_HOLD_BETWEEN_SIDES: {
          if (currentTime - _distractedPhaseStartTime >= DISTRACTED_NEUTRAL_HOLD_DURATION) {
            EyeState targetRightEye = {15, 0, EYE_WIDTH * 1.5, EYE_HEIGHT * 1.5};
            EyeState targetLeftEye = {10, 0, EYE_WIDTH * 0.5, EYE_HEIGHT * 0.5};
            startAnimation(targetLeftEye, targetRightEye, DISTRACTED_ANIM_DURATION);
            _distractedPhase = DISTRACTED_RIGHT_ANIM;
            _distractedPhaseStartTime = currentTime;
          }
          break;
        }
        case DISTRACTED_RIGHT_ANIM: {
          if (_animDuration == 0) {
            _distractedPhase = DISTRACTED_RIGHT_HOLD;
            _distractedPhaseStartTime = currentTime;
          }
          break;
        }
        case DISTRACTED_RIGHT_HOLD: {
          if (currentTime - _distractedPhaseStartTime >= DISTRACTED_SIDE_HOLD_DURATION) {
            EyeState neutralTarget = {0, 0, EYE_WIDTH, EYE_HEIGHT};
            startAnimation(neutralTarget, DISTRACTED_ANIM_DURATION);
            _distractedPhase = DISTRACTED_RETURN_TO_NEUTRAL_FROM_RIGHT;
            _distractedPhaseStartTime = currentTime;
          }
          break;
        }
        case DISTRACTED_RETURN_TO_NEUTRAL_FROM_RIGHT: {
          if (_animDuration == 0) {
            _distractedPhase = DISTRACTED_NEUTRAL_HOLD_BETWEEN_SIDES;
            _distractedPhaseStartTime = currentTime;
          }
          break;
        }
      }
  } else if (_currentEmotion == SLEEP) {
      // SLEEP logic
      // _vibrateXOffset is 0 from the 'else' block above.
      // _vibrateYOffset is controlled here for mouth movement.
      if (currentTime - _sleepMouthLastToggleTime > SLEEP_MOUTH_TOGGLE_INTERVAL) {
        _sleepMouthLastToggleTime = currentTime;
        if (_sleepMouthState == SLEEP_MOUTH_UNSHAPED) {
          _sleepMouthState = SLEEP_MOUTH_OVAL;
          _vibrateYOffset = -2; // Move eyes/mouth up slightly for snoring
        } else {
          _sleepMouthState = SLEEP_MOUTH_UNSHAPED;
          _vibrateYOffset = 0; // Return to normal Y position
        }
      }
  } else if (_currentEmotion == SCARED) {
      // SCARED logic
      // _vibrateXOffset and _vibrateYOffset are set to 0 by the 'else' block above.
      // SCARED uses _currentState offsets for its movement, updated by startAnimation.
      if (_animDuration == 0) { // Animation complete, start a new quick glance
        EyeState glanceTarget = {(float)random(-8, 9), (float)random(-6, 7), _currentState[0].width, _currentState[0].height};
        startAnimation(glanceTarget, 100); // Very quick animation duration
      }
  }
  
  drawEyes();
}

void RideBuddyEyes::setEmotion(Emotion emotion) {
  if (_currentEmotion == emotion && emotion != BLINK) return;
  if (_currentEmotion != BLINK) _previousEmotion = _currentEmotion;
  _currentEmotion = emotion;

  _isIdleActionActive = false;
  
  if (emotion == CRY) {
      _cry_anim_currentFrame = 0;
      _cry_anim_lastFrameTime = millis();
      return; 
  }
  if (emotion == SHY) { // Handle SHY as a full-screen animation like CRY
      _shy_anim_currentFrame = 0;
      _shy_anim_lastFrameTime = millis();
      return;
  }
  if (emotion == DRIVING) {
      _driving_anim_currentFrame = 0;
      _driving_anim_lastFrameTime = millis();
      return;
  }
  if (emotion == HAPPY) { // Handle HAPPY as a full-screen animation
      _happy_anim_currentFrame = 0;
      _happy_anim_lastFrameTime = millis();
      return;
  }
  if (emotion == BATTERY) { // Handle BATTERY as a full-screen animation
      _battery_anim_currentFrame = 0;
      _battery_anim_lastFrameTime = millis();
      return;
  }
  if (emotion == SLEEP) {
      _sleepMouthState = SLEEP_MOUTH_UNSHAPED;
      _sleepMouthLastToggleTime = millis();
      // Eye state for sleep is already handled in switch case
      return;
  }
  if (emotion == DISTRACTED) {
      _distractedPhase = DISTRACTED_INIT;
      _distractedPhaseStartTime = millis();
      return;
  }

  EyeState target;
  target.xOffset = 0;
  target.yOffset = 0;

  switch (emotion) {
    case HAPPY:
      // No procedural animation for HAPPY, handled by bitmap
      break;
    case LOVE:
      // No procedural animation for LOVE, handled by bitmap
      break;
    case BLINK:
      target.width = _currentState[0].width;
      target.height = _currentState[0].height;
      startAnimation(target, 0);
      break;
    case SLEEP:
      target.width = EYE_WIDTH;
      target.height = EYE_WIDTH / 2; // Give it enough height to show the curve
      target.yOffset = 0; 
      startAnimation(target, 200);
      break;
    case SCARED:
      target.width = EYE_WIDTH * 0.7;
      target.height = EYE_HEIGHT * 0.7;
      target.xOffset = 0;
      target.yOffset = 0;
      startAnimation(target, 150);
      break;
    default: // NEUTRAL
      target.width = EYE_WIDTH;
      target.height = EYE_HEIGHT;
      startAnimation(target, 150);
      break;
  }
}

// --- Public API Functions ---
void RideBuddyEyes::neutral() { setEmotion(NEUTRAL); }
void RideBuddyEyes::happy()   { setEmotion(HAPPY); }
void RideBuddyEyes::cry()     { setEmotion(CRY); }
void RideBuddyEyes::love()    { setEmotion(LOVE); }
void RideBuddyEyes::shy()     { setEmotion(SHY); }
void RideBuddyEyes::angry()   { setEmotion(ANGRY); }
void RideBuddyEyes::driving() { setEmotion(DRIVING); }
void RideBuddyEyes::distracted() { setEmotion(DISTRACTED); }
void RideBuddyEyes::sleep() { setEmotion(SLEEP); }
void RideBuddyEyes::scared() { setEmotion(SCARED); }
void RideBuddyEyes::battery() { setEmotion(BATTERY); }
void RideBuddyEyes::blink()   { if (_currentEmotion != BLINK) { _blinkStartTime = millis(); setEmotion(BLINK); } }

// --- Animation System ---
void RideBuddyEyes::startAnimation(const EyeState& target, uint16_t duration) {
  _animStartTime = millis();
  _animDuration = duration;
  for (int i = 0; i < 2; i++) { 
      _startState[i] = _currentState[i];
      _targetState[i] = target;
  }
}

void RideBuddyEyes::startAnimation(const EyeState& targetLeft, const EyeState& targetRight, uint16_t duration) {
  _animStartTime = millis();
  _animDuration = duration;
  _startState[0] = _currentState[0];
  _targetState[0] = targetLeft;
  _startState[1] = _currentState[1];
  _targetState[1] = targetRight;
}

void RideBuddyEyes::updateAnimation() {
  if (_animDuration == 0) return;
  float progress = (float)(millis() - _animStartTime) / _animDuration;
  if (progress >= 1.0) { progress = 1.0; _animDuration = 0; }
  float easedProgress = easeInOut(progress);
  for (int i = 0; i < 2; i++) {
    _currentState[i].xOffset = _startState[i].xOffset + (_targetState[i].xOffset - _startState[i].xOffset) * easedProgress;
    _currentState[i].yOffset = _startState[i].yOffset + (_targetState[i].yOffset - _startState[i].yOffset) * easedProgress;
    _currentState[i].width = _startState[i].width + (_targetState[i].width - _startState[i].width) * easedProgress;
    _currentState[i].height = _startState[i].height + (_targetState[i].height - _startState[i].height) * easedProgress;
  }
}

float RideBuddyEyes::easeInOut(float t) { return t < 0.5 ? 4 * t * t * t : 1 - pow(-2 * t + 2, 3) / 2; }


// --- Drawing ---
void RideBuddyEyes::drawSleepZzz(int eye_x, int eye_y, int eye_w, int eye_h) {
      // This function assumes eye_x, eye_y, eye_w, eye_h are provided for the specific eye
      // where the 'zzz' should be drawn (e.g., the right eye).
          _display->setFont(u8g2_font_ncenB08_tr);
          _display->setDrawColor(1); // Ensure drawing in white

          // Position 'zzz' relative to the top-right of the provided eye's coordinates.
          // eye_x, eye_y are the center of the current eye.
          // zzz_base_x is calculated to be to the right of the eye's center by half its width, plus an offset.
          int zzz_base_x = eye_x + eye_w / 2 + 5; // Right of the eye
          // zzz_base_y is calculated to be above the eye's top edge, with a smaller offset to keep it closer.
          int zzz_base_y = eye_y - eye_h / 2 - 5; // Near top of the eye
      
          // Draw 'zzz' text
          _display->setCursor(zzz_base_x, zzz_base_y);
          _display->print("z");
          _display->setCursor(zzz_base_x + 8, zzz_base_y - 8);
          _display->print("z");
          _display->setCursor(zzz_base_x + 16, zzz_base_y - 16);
          _display->print("z");
}

void RideBuddyEyes::drawMouth(int x, int y, int w, int h) {
  _display->setDrawColor(1); // Draw white for the base ellipse
  _display->drawFilledEllipse(x, y, w / 2, h / 2);
  _display->setDrawColor(0); // Set color to black for cutting the top
  _display->drawBox(x - w / 2, y - h / 2, w, h / 2);
  _display->setDrawColor(1); // Reset color to white
}

void RideBuddyEyes::drawEyes() {
  if (!_display) return;
  
  _display->clearBuffer();
  _display->setDrawColor(1); // Default drawing color for white shapes

  unsigned long currentTime = millis(); // Get current time once for potential use in all drawing logic

  // --- Handle full-screen bitmap animations (CRY, SHY, ANGRY, DRIVING, HAPPY, BATTERY, LOVE) ---
  if (_currentEmotion == CRY || _currentEmotion == SHY || _currentEmotion == ANGRY || _currentEmotion == DRIVING || _currentEmotion == HAPPY || _currentEmotion == BATTERY || _currentEmotion == LOVE) {
    if (_currentEmotion == CRY) {
      if (currentTime - _cry_anim_lastFrameTime > CRY_FRAME_DURATION) {
        _cry_anim_lastFrameTime = currentTime;
        _cry_anim_currentFrame = (_cry_anim_currentFrame + 1) % CRY_FRAME_COUNT;
      }
      _display->drawXBMP(0, 0, 128, 64, (const unsigned char*)pgm_read_dword(&cry_frames[_cry_anim_currentFrame]));
    } else if (_currentEmotion == SHY) { // SHY animation logic
      if (currentTime - _shy_anim_lastFrameTime > SHY_FRAME_DURATION) {
        _shy_anim_lastFrameTime = currentTime;
        _shy_anim_currentFrame = (_shy_anim_currentFrame + 1) % SHY_FRAME_COUNT;
      }
      _display->drawXBMP(0, 0, 128, 64, (const unsigned char*)pgm_read_dword(&shy_frames[_shy_anim_currentFrame]));
    } else if (_currentEmotion == ANGRY) { // ANGRY single bitmap
      _display->drawXBMP(_vibrateXOffset, _vibrateYOffset, 128, 64, angry_bits);
    } else if (_currentEmotion == DRIVING) { // DRIVING animation logic
      if (currentTime - _driving_anim_lastFrameTime > DRIVING_FRAME_DURATION) {
        _driving_anim_lastFrameTime = currentTime;
        _driving_anim_currentFrame = (_driving_anim_currentFrame + 1) % DRIVING_FRAME_COUNT;
      }
      _display->drawXBMP(0, 0, 128, 64, (const unsigned char*)pgm_read_dword(&driving_frames[_driving_anim_currentFrame]));
    } else if (_currentEmotion == HAPPY) { // HAPPY animation logic
      if (currentTime - _happy_anim_lastFrameTime > HAPPY_FRAME_DURATION) {
        _happy_anim_lastFrameTime = currentTime;
        _happy_anim_currentFrame = (_happy_anim_currentFrame + 1) % HAPPY_FRAME_COUNT;
      }
      _display->drawXBMP(0, 0, 128, 64, (const unsigned char*)pgm_read_dword(&happy_frames[_happy_anim_currentFrame]));
    } else if (_currentEmotion == BATTERY) { // BATTERY animation logic
      if (currentTime - _battery_anim_lastFrameTime > BATTERY_FRAME_DURATION) {
        _battery_anim_lastFrameTime = currentTime;
        _battery_anim_currentFrame = (_battery_anim_currentFrame + 1) % BATTERY_FRAME_COUNT;
      }
      _display->drawXBMP(0, 0, 128, 64, (const unsigned char*)pgm_read_dword(&battery_frames[_battery_anim_currentFrame]));
    } else if (_currentEmotion == LOVE) { // LOVE single bitmap
      _display->drawXBMP(_vibrateXOffset, _vibrateYOffset, 128, 64, love_bits);
    }
    _display->sendBuffer(); // Update display for bitmap emotions
    return; // Exit as bitmap emotions take full screen
  }

  // --- Handle procedural animations (NEUTRAL, BLINK, DISTRACTED, SLEEP) ---
  for (int i = 0; i < 2; i++) { 
    drawOneEye(i, _currentEmotion); 
  }

  if (_currentEmotion == NEUTRAL) {
    _display->setDrawColor(1); // Ensure white for mouth
    float mouthX = SCREEN_WIDTH / 2 + _currentState[0].xOffset + _vibrateXOffset;
    float mouthY = _eyeCenterY[0] + (EYE_HEIGHT / 2) + 8 + _currentState[0].yOffset + _vibrateYOffset; // Position below neutral eyes
    float mouthW = 20;
    float mouthH = 8;
    drawMouth(mouthX, mouthY, mouthW, mouthH);
  } else if (_currentEmotion == SCARED) {
    _display->setDrawColor(1); // Ensure white for eyebrows and mouth
    // Eyebrows for scared emotion
    for (int i = 0; i < 2; i++) {
      // Round float coordinates to integers for drawing consistency
      int current_eye_x = round(_eyeCenterX[i] + _currentState[i].xOffset);
      int current_eye_y = round(_eyeCenterY[i] + _currentState[i].yOffset);
      int current_eye_half_width = round(_currentState[i].width / 2);
      int current_eye_half_height = round(_currentState[i].height / 2);

      int eyebrow_y_base = current_eye_y - current_eye_half_height - 5; // Position above the eye

      // Define eyebrow start and end points
      int x1, y1, x2, y2;

      if (i == 0) { // Left eye
        x1 = current_eye_x - current_eye_half_width + 5;
        y1 = eyebrow_y_base;
        x2 = current_eye_x + 5;
        y2 = eyebrow_y_base - 8;
      } else { // Right eye
        x1 = current_eye_x + current_eye_half_width - 5;
        y1 = eyebrow_y_base;
        x2 = current_eye_x - 5;
        y2 = eyebrow_y_base - 8;
      }

      // Manual clamping of coordinates to screen bounds
      x1 = constrain(x1, 0, SCREEN_WIDTH - 1);
      y1 = constrain(y1, 0, SCREEN_HEIGHT - 1);
      x2 = constrain(x2, 0, SCREEN_WIDTH - 1);
      y2 = constrain(y2, 0, SCREEN_HEIGHT - 1);

      _display->drawLine(x1, y1, x2, y2);
    }

    // Mouth for scared emotion
    int mouthX = SCREEN_WIDTH / 2 + _currentState[0].xOffset; // Use _currentState.xOffset
    int mouthY = _eyeCenterY[0] + (EYE_HEIGHT / 2) + 12 + _currentState[0].yOffset; // Use _currentState.yOffset
    int mouthW = 10; // Small circular mouth
    _display->drawDisc(mouthX, mouthY, mouthW / 2);
  } else if (_currentEmotion == SLEEP) {
    _display->setDrawColor(1); // Ensure white for mouth and bubbles
    int mouthX = SCREEN_WIDTH / 2;
    int mouthY = 58 + _vibrateYOffset; // Position below the eyes
    int mouthW, mouthH;

    if (_sleepMouthState == SLEEP_MOUTH_UNSHAPED) {
      mouthW = 20; // Unshaped (more circular)
      mouthH = 8;
    } else { // SLEEP_MOUTH_OVAL
      mouthW = 28; // Oval (wider, shallower)
      mouthH = 6;
    }
    drawMouth(mouthX, mouthY, mouthW, mouthH);

    // Draw "sleep bubbles" near the mouth
    _display->drawCircle(mouthX + 15, mouthY - 10, 8); // Main bubble
    _display->drawCircle(mouthX + 25, mouthY - 20, 4); // Smaller bubble trailing

    // Right eye is eye index 1
    int right_eye_x = _eyeCenterX[1] + _currentState[1].xOffset + _vibrateXOffset;
    int right_eye_y = _eyeCenterY[1] + _currentState[1].yOffset + _vibrateYOffset;
    int right_eye_w = _currentState[1].width;
    int right_eye_h = _currentState[1].height;

  }

  _display->sendBuffer(); // Update display once for procedural emotions
}

void RideBuddyEyes::drawOneEye(uint8_t i, Emotion emotion) {
    float x = _eyeCenterX[i] + _currentState[i].xOffset;
    float y = _eyeCenterY[i] + _currentState[i].yOffset;
    float w = _currentState[i].width;
    float h = _currentState[i].height;

    _display->setDrawColor(1); // Ensure drawing in white by default for eyes

    switch (emotion) {
      case ANGRY: {
        // ANGRY emotion is now handled as a bitmap display in drawEyes()
        break;
      }
      case BLINK: {
        _display->drawHLine(x - w/2, y, w);
        break;
      }
      case SHY: {
        // SHY emotion is now handled as a bitmap animation in drawEyes()
        break;
      }
      case DRIVING: {
        // DRIVING emotion is now handled as a bitmap animation in drawEyes()
        break;
      }
      case SLEEP: {
        // "U" Shape Logic
        int radius = w / 2;
        if (radius < 2) radius = 2; // Safety check
        int thickness = 3; // How thick the U line is

        _display->setDrawColor(1); // Draw white for the outer disc
        _display->drawDisc(x, y, radius);

        _display->setDrawColor(0); // Draw black for the inner disc to make a ring
        _display->drawDisc(x, y, radius - thickness);
        
        _display->setDrawColor(0); // Draw black for the box to cut into U shape
        _display->drawBox(x - radius, y - radius, radius * 2 + 1, radius + 1);
        _display->setDrawColor(1); // Reset to white for subsequent drawings
        break;
      }
      case SCARED: {
        int outer_radius = w / 2;
        int inner_radius = outer_radius - 2; // Make inner circle slightly smaller
        if (inner_radius < 1) inner_radius = 1; // Safety check

        _display->setDrawColor(1); // Draw white outer circle
        _display->drawDisc(x, y, outer_radius); 
        
        _display->setDrawColor(0); // Draw black inner circle
        _display->drawDisc(x, y, inner_radius);  
        
        _display->setDrawColor(1); // Draw white pupil
        _display->drawDisc(x, y, inner_radius / 2); 
        break;
      }
      default: { // NEUTRAL, DISTRACTED, etc.
        // Calculate a safe corner radius that does not exceed half the smallest dimension
        int currentRadius = EYE_CORNER_RADIUS;
        currentRadius = min(currentRadius, (int)(h / 2.0));
        currentRadius = min(currentRadius, (int)(w / 2.0));
        if (currentRadius < 0) currentRadius = 0; 
        
        _display->drawRBox(x - w / 2, y - h / 2, w, h, currentRadius);
        break;
      }
    }
}