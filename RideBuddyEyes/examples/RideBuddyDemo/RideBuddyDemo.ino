/*
  RideBuddyDemo.ino - Example sketch for the RideBuddyEyes library.
  
  This sketch demonstrates how to cycle through the available emotions
  by touching a sensor.

  Hardware:
  - Arduino Nano
  - SH1106 128x64 I2C OLED Display (SDA: A4, SCL: A5)
  - TTP223 Touch Sensor (on pin D2)
  
  Interaction:
  - Touch the sensor to cycle through the emotions.
  
  Created by Gemini, 2025.
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "RideBuddyEyes.h"

// --- Pin Definitions ---
#define TOUCH_PIN 2

// --- Global Objects ---
Adafruit_SH1106G display(128, 64, &Wire, -1);
RideBuddyEyes eyes;

// --- Touch Sensor State ---
bool touchActiveState;
bool lastReading = false;
bool debouncedState = false;
unsigned long lastDebounceTime = 0;
#define DEBOUNCE_DELAY 50

// --- Emotion Cycling ---
int currentEmotionIndex = 0;
const int NUM_EMOTIONS = 8; // Updated: 8 emotions (added happy)

void setup() {
  Serial.begin(115200);
  
  if(!display.begin(0x3C, true)) { 
    Serial.println(F("SH1106 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.display();

  pinMode(TOUCH_PIN, INPUT); 

  eyes.begin(&display);
  
  Serial.println(F("RideBuddy Eyes Demo"));
  Serial.println(F("Touch sensor to cycle through emotions."));
}

void loop() {
  handleTouch();
  eyes.update(); // This handles blinking and drawing
}

void handleTouch() {
  bool reading = digitalRead(TOUCH_PIN);

  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != debouncedState) {
      debouncedState = reading;
      if (debouncedState == HIGH) { // Assuming touch sensor goes HIGH when touched
        triggerNextEmotion();
      }
    }
  }
  lastReading = reading;
}

void triggerNextEmotion() {
  Serial.print(F("Touch! Triggering emotion #"));
  Serial.print(currentEmotionIndex);
  Serial.print(F(": "));

  switch(currentEmotionIndex) {
    case 0: 
      Serial.println(F("Neutral")); 
      eyes.neutral(); 
      break;
    case 1: 
      Serial.println(F("Sad")); 
      eyes.sad(); 
      break;
    case 2:
      Serial.println(F("Angry"));
      eyes.angry();
      break;
    case 3:
      Serial.println(F("Love"));
      eyes.love();
      break;
    case 4:
      Serial.println(F("Scared"));
      eyes.scared();
      break;
    case 5:
      Serial.println(F("Pat"));
      eyes.pat();
      break;
    case 6:
      Serial.println(F("Serious"));
      eyes.serious();
      break;
    case 7:
      Serial.println(F("Happy"));
      eyes.happy();
      break;
  }
  currentEmotionIndex = (currentEmotionIndex + 1) % NUM_EMOTIONS;
}