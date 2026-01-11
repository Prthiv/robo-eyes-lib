# RideBuddyEyes Library Guide

The `RideBuddyEyes` library provides an easy and expressive way to add animated robot eyes to your Arduino projects, specifically designed for SH110X-based OLED displays using the U8g2 library. It offers a range of emotions and smooth transitions, making your robotic creations more engaging.

## Dependencies

This library relies on the following Arduino library:

*   **`U8g2 Library`**: A universal graphics library for OLEDs and LCDs.

## Installation

1.  **Include the library:** Place the `RideBuddyEyes.h` and `RideBuddyEyes.cpp` files in your Arduino project folder.
2.  **Install dependencies:** Ensure you have the `U8g2 Library` installed in your Arduino IDE (Sketch > Include Library > Manage Libraries...).

## Usage Guide

### 1. Include Headers

In your main Arduino sketch (`.ino` file), include the `RideBuddyEyes.h` header, along with the necessary display library header:

```cpp
#include <U8g2lib.h>
#include <RideBuddyEyes.h>
#include <Wire.h> // Required for I2C communication
```

### 2. Initialize your OLED Display (U8g2)

Before using `RideBuddyEyes`, you need to set up your U8g2 display object. The `RideBuddyEyes` library is designed to work with U8g2's `U8G2` object. Ensure you select the correct constructor for your specific display (e.g., SH1106 128x64, I2C).

Example for an I2C SH1106 128x64 display (replace with your display's specific constructor):

```cpp
// U8g2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE); // full framebuffer, I2C
// For SH1106 I2C, 128x64, no reset pin, hardware I2C
U8g2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE); 

// If you need a specific I2C address, you might use:
// U8g2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, /* clock=*/ SCL, /* data=*/ SDA); 
// and then use u8g2.setI2CAddress(0x3C*2); in setup()
```
_Refer to the [U8g2 Setup Guide](https://github.com/olikraus/u8g2/wiki/u8g2setupcpp) for precise display initialization for your specific hardware._

### 3. Create a `RideBuddyEyes` Object

Declare an instance of the `RideBuddyEyes` class:

```cpp
RideBuddyEyes eyes;
```

### 4. Initialize the Library in `setup()`

In your `setup()` function, initialize the display and then pass the display object to the `eyes.begin()` method, along with your I2C SDA and SCL pins.

```cpp
void setup() {
  Serial.begin(115200);

  // Initialize display
  u8g2.begin();
  u8g2.setBusClock(400000); // Set I2C speed to 400kHz for faster updates

  // Initialize RideBuddyEyes
  // Replace SDA_PIN and SCL_PIN with your actual I2C pins
  eyes.begin(&u8g2, SDA_PIN, SCL_PIN); 
  eyes.neutral(); // Set initial emotion
}
```

### 5. Update the Eyes in `loop()`

Crucially, you must call `eyes.update()` regularly within your `loop()` function. This function handles all the animation, emotion transitions, and redrawing of the eyes.

```cpp
void loop() {
  eyes.update();
  // Your other loop code here
}
```

### 6. Setting Emotions

You can change the robot's emotion using the `setEmotion()` method or dedicated helper functions.

**Using `setEmotion()`:**

```cpp
eyes.setEmotion(NEUTRAL);
eyes.setEmotion(CRY);
eyes.setEmotion(ANGRY);
// ... etc.
```

**Using helper functions:**

```cpp
eyes.neutral();
eyes.cry();
eyes.angry();
// ... etc.
```

The available emotions are:

*   **`NEUTRAL`**: Standard, calm eyes (rounded rectangles).
*   **`BLINK`**: A quick blink.
*   **`HAPPY`**: Animated happy eyes.
*   **`CRY`**: Animated crying eyes.
*   **`LOVE`**: Static love heart.
*   **`SHY`**: Animated shy eyes.
*   **`ANGRY`**: Static angry eyes.
*   **`DRIVING`**: Animated driving eyes.
*   **`SCARED`**: Smaller, rounded eyes that dart around with animated eyebrows.
*   **`DISTRACTED`**: Eyes dynamically scale and look around.
*   **`SLEEP`**: Sleeping eyes with a "zzz" animation.
*   **`BATTERY`**: Animated battery level eyes.

### Example Sketch Structure

```cpp
#include <U8g2lib.h>
#include <RideBuddyEyes.h>
#include <Wire.h> 

// For SH1106 I2C, 128x64, no reset pin, hardware I2C
// Check your display's specification for the correct constructor
U8g2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE); 

// Define your I2C pins if different from default or if required by eyes.begin()
#define SDA_PIN 21 
#define SCL_PIN 22

RideBuddyEyes eyes;

void setup() {
  Serial.begin(115200);

  // Initialize display
  u8g2.begin();
  u8g2.setBusClock(400000); // Set I2C speed to 400kHz

  // Initialize RideBuddyEyes
  eyes.begin(&u8g2, SDA_PIN, SCL_PIN); 
  eyes.neutral(); // Set initial emotion
}

void loop() {
  eyes.update();

  // Example: Cycle through emotions every few seconds
  static unsigned long lastEmotionChange = 0;
  static int emotionIndex = 0;
  unsigned long currentTime = millis();

  if (currentTime - lastEmotionChange > 4000) { // Change emotion every 4 seconds
    lastEmotionChange = currentTime;
    emotionIndex = (emotionIndex + 1) % 12; // Cycle through all 12 emotions

    switch(emotionIndex) {
      case 0: eyes.neutral(); Serial.println("Neutral"); break;
      case 1: eyes.happy(); Serial.println("Happy"); break;
      case 2: eyes.cry(); Serial.println("Cry"); break;
      case 3: eyes.love(); Serial.println("Love"); break;
      case 4: eyes.shy(); Serial.println("Shy"); break;
      case 5: eyes.angry(); Serial.println("Angry"); break;
      case 6: eyes.driving(); Serial.println("Driving"); break;
      case 7: eyes.scared(); Serial.println("Scared"); break;
      case 8: eyes.distracted(); Serial.println("Distracted"); break;
      case 9: eyes.sleep(); Serial.println("Sleep"); break;
      case 10: eyes.battery(); Serial.println("Battery"); break;
      case 11: eyes.blink(); Serial.println("Blink (will revert to previous emotion)"); break;
    }
  }
}