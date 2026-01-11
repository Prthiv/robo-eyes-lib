#include <Wire.h>
#include <U8g2lib.h>
#include "RideBuddyEyes.h"

// --- Pin Definitions ---
#define I2C_SDA 21
#define I2C_SCL 22

// --- OLED Display Setup ---
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ I2C_SCL, /* data=*/ I2C_SDA);

// --- Eyes Setup ---
RideBuddyEyes eyes;

void setup() {
  Serial.begin(115200);
  
  u8g2.begin();
  
  eyes.begin(&u8g2, I2C_SDA, I2C_SCL);
  eyes.neutral(); 
}

void loop() {
  // Cycle through all the emotions
  eyes.happy();
  eyes.update();
  delay(2000);

  eyes.neutral();
  eyes.update();
  delay(2000);

  eyes.cry();
  eyes.update();
  delay(2000);

  eyes.love();
  eyes.update();
  delay(2000);

  eyes.shy();
  eyes.update();
  delay(2000);

  eyes.angry();
  eyes.update();
  delay(2000);

  eyes.driving();
  eyes.update();
  delay(2000);

  eyes.distracted();
  eyes.update();
  delay(2000);

  eyes.sleep();
  eyes.update();
  delay(2000);

  eyes.scared();
  eyes.update();
  delay(2000);
}