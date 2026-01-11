#include <Arduino.h>
#include <math.h>
#include <ArduinoJson.h>
#include <MPU6050_light.h>
#include "RideBuddyEyes.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <time.h>
#include <HTTPClient.h>
#include <DNSServer.h>
#include <rom/rtc.h> // Required for RTC slow memory access

// ==================================================
// PINS & CONFIGURATION
// ==================================================
#define I2C_SDA 21
#define I2C_SCL 22
#define TOUCH_PIN 27
#define BUTTON_PIN 25
#define BATTERY_PIN 32 // ADC pin for battery voltage monitoring
#define CHARGING_STATUS_PIN 19 // GPIO connected to TP4056 STAT pin
#define CHARGING_STATUS_PIN_LOGIC LOW // Logic level when charging (e.g., LOW for active)

// --- Battery Monitoring Constants ---
#define VOLTAGE_DIVIDER_RATIO 2.0f // R2 / (R1 + R2), assuming 2x 4.7k resistors gives 0.5 ratio, so actual voltage is ADC_Voltage * 2.0

// --- Ride Buddy Tuning ---
#define MOVEMENT_THRESHOLD 0.40
#define HOLD_TIME 1000
#define DEBOUNCE_DELAY 50
#define BOREDOM_TIME 10000
#define SLEEP_TIME   15000
#define DRIVING_LIMIT 15000
#define SLEEP_EMOTION_DURATION 5000 // How long sleep emotion is shown before deep sleep (5 seconds)

// --- Desk Buddy ---
const byte DNS_PORT = 53;
const char* SETUP_SSID = "SmartClock-Setup";
IPAddress apIP(192,168,4,1);
IPAddress netMsk(255,255,255,0);

// ==================================================
// OBJECTS
// ==================================================
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* scl=*/ 22, /* sda=*/ 21);

MPU6050 mpu(Wire);
RideBuddyEyes eyes;

DNSServer dnsServer;
WebServer server(80);
Preferences prefs;

// ==================================================
// GLOBAL STATE
// ==================================================
// RTC data structure for persisting time across deep sleep
typedef struct {
  time_t lastEpochTime;
  uint32_t bootCount; // Optional: to track number of boots
} rtc_store_t;

RTC_DATA_ATTR rtc_store_t rtcData;

enum AppMode { 
  MODE_EYES,          // Showing animated eyes (default)
  MODE_CLOCK,         // Showing time + weather
  MODE_MENU, 
  MODE_STOPWATCH, 
  MODE_PORTAL, 
  MODE_RESET_CONFIRM,
  MODE_SHUTDOWN_CONFIRM,
  MODE_GAME_MENU,
  MODE_GAME_FLAPPY,
  MODE_GAME_RACING
};
AppMode currentAppMode = MODE_EYES;

// --- Game Menu State ---
int gameMenuSelection = 0; // 0 = Flappy, 1 = Racing

// --- Flappy Game Globals ---
float birdY = 32;     // Vertical position
float birdV = 0;      // Vertical velocity
int pipeX = 128;      // Pipe X position
int pipeGapY = 30;    // Vertical center of the gap
int score = 0;
bool isGameOver = false;
bool gameStarted = false; 
// -------------------------

// --- Racing Game Globals ---
float raceCarX = 64;  // Center of screen (0-128)
int raceScore = 0;
float raceSpeed = 2.0;
unsigned long raceGameOverTime = 0; // Timer for retry delay
struct RaceObstacle {
  float x;      // Horizontal position (-1 to 1 normalized to road width)
  float z;      // Distance (100 = far, 0 = near)
  bool active;
};
RaceObstacle raceObstacles[3]; // Max 3 obstacles on screen
// -------------------------

// --- Animation State --- 
static bool runClockStartupAnimation = false;
static unsigned long clockAnimStartTime = 0;

bool wifiConnected = false;
bool isFirstSetup = false; // Added from server code
bool isTimeSynced = false;
bool mpuOK = false;

byte menuSelection = 1;
byte brightnessLevel = 255;

// Battery monitoring state
byte LOW_BATTERY_THRESHOLD = 5; // Define threshold globally (e.g., 5%)
int lastDisplayedBatteryPct = 0; // Stores last known battery percentage when not charging

// Weather

String weatherTemp = "--";

char weatherIcon = ' ';

String locationName = "---"; // New global variable for dynamic location 
// Rolling average for battery smoothing

float batteryReadings[10] = {0};

int readingIndex = 0;

float smoothedBatteryVoltage = 0;

unsigned long lastBatteryReadTime = 0; 
// Stopwatch
unsigned long stopwatchStart = 0, stopwatchElapsed = 0;
bool stopwatchRunning = false;

// Button state
unsigned long buttonPressStartTime = 0, lastReleaseTime = 0;
const unsigned long doubleClickGap = 250;
const unsigned long longPressThreshold = 2000;
bool buttonDown = false; // Kept existing `buttonDown` for button state

// Global time tracker
unsigned long currentTime = 0; // Made global for function access
unsigned long lastPeriodicSyncTime = 0; // Re-added for periodic time sync

// Ride Buddy state (unchanged)
unsigned long lastInteractionTime = 0;
unsigned long touchStartTime = 0;
unsigned long motionStartTime = 0;
unsigned long stopSequenceStartTime = 0;
unsigned long lastMotionTriggerTime = 0;
byte tapCount = 0;
bool isTouching = false;
bool isSleeping = false;
bool ignoreCurrentTouch = false;
String currentMood = "NEUTRAL";
unsigned long sleepEmotionDisplayTime = 0;
bool oledIsOffForIdle = false; // Flag to track if OLED is off due to idle sleep

bool isMoving = false;
bool isDriving = false;
bool stopSequenceActive = false;
float lastX, lastY, lastZ;

byte lastSteadyState = LOW;
byte lastFlickerableState = LOW;
unsigned long lastDebounceTime = 0;

// ==================================================
// FORWARD DECLARATIONS
// ==================================================
void drawMenu();
void updateOLEDClock();

// ==================================================
// SETUP
// ==================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  
  isTimeSynced = false;
  
  // **CHECK WHY WE WOKE UP**
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  bool wokeFromSleep = false;
  
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("🔌 POWERED ON by BUTTON!");
    wokeFromSleep = true;
  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
    Serial.println("🔌 NORMAL POWER ON (USB plugged in or RST pressed)");
  }
  
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(CHARGING_STATUS_PIN, INPUT_PULLUP); // Configure charging status pin once
  
  // Set ADC attenuation for battery pin - CRITICAL for correct voltage reading
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
  pinMode(BATTERY_PIN, INPUT);   // safety
  
  // Initialize MPU6050 & Wire
  Wire.begin(I2C_SDA, I2C_SCL);

  // Initialize Display
  u8g2.begin();
  
  // Load and apply brightness from preferences
  prefs.begin("display", true);
  brightnessLevel = prefs.getUChar("bright", 255); // Default to 255 (max)
  prefs.end();
  u8g2.setContrast(brightnessLevel);

  u8g2.clearBuffer();
  u8g2.setBusClock(400000);
  u8g2.setFont(u8g2_font_logisoso16_tf);
  
  // **SHOW DIFFERENT SPLASH BASED ON WAKEUP**
  if (wokeFromSleep) {
    drawCenteredStr(40, "POWERING ON");
    u8g2.sendBuffer();
    delay(1000);
  } else {
    drawCenteredStr(40, "SMART CLOCK");
    u8g2.sendBuffer();
    delay(1500);
  }

  // MPU6050
  mpuOK = (mpu.begin() == 0); // Set flag here
  if (!mpuOK) { // Check the flag
    // MPU Failed, proceed without it
  } else {
    delay(1000);
    mpu.calcOffsets(); // Only call if MPU is OK
  }

  // Eyes
  eyes.begin(&u8g2, I2C_SDA, I2C_SCL);
  eyes.neutral();
  lastInteractionTime = millis(); // Crucial for RideBuddy idle logic

  // Increment boot count
  rtcData.bootCount++;

  // Initialize time from RTC data first (if available)
  if (rtcData.lastEpochTime > 1609459200) { // Check for a valid epoch time (e.g., after 2021-01-01)
    struct timeval tv = { .tv_sec = rtcData.lastEpochTime, .tv_usec = 0 };
    settimeofday(&tv, NULL);
  }

  // Check if system time is valid after potential RTC restore
  time_t now = 0;
  struct tm ti;
  time(&now);
  localtime_r(&now, &ti);
  if (ti.tm_year > (2021 - 1900)) { // Check for year > 2020
    isTimeSynced = true;
  }

  // Try saved WiFi
  prefs.begin("wifi", true);
  String savedSSID = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();

  if (savedSSID == "") { // First time setup or factory reset
    isFirstSetup = true; 
    WiFi.softAPConfig(apIP, apIP, netMsk); WiFi.softAP(SETUP_SSID); dnsServer.start(DNS_PORT, "*", apIP);
    currentAppMode = MODE_PORTAL; 
    wifiConnected = false; // Ensure wifiConnected is false for portal
  } else { // Attempt to connect to saved WiFi
    isFirstSetup = false;
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tr);
    drawCenteredStr(32, "Connecting WiFi...");
    u8g2.sendBuffer();

    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    unsigned long st = millis(); 
    while (WiFi.status() != WL_CONNECTED && millis() - st < 8000) { // 8 second timeout
      delay(10);
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      configTime(19800, 0, "pool.ntp.org");  // IST

      // Wait for NTP to sync
      struct tm timeinfo;
      int retry = 0;
      while (!getLocalTime(&timeinfo) || timeinfo.tm_year < 100) {
        delay(500);
        if (++retry > 10) break; // Timeout after 5s
      }

      if (timeinfo.tm_year > 100) { // Sync successful
        isTimeSynced = true;
        fetchWeather();
        // Store current epoch time in RTC memory
        time_t now;
        time(&now);
        rtcData.lastEpochTime = now;
      }

      // Disconnect WiFi completely to save power
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      btStop();
      wifiConnected = false; // Set flag to false as WiFi is now off
    } else {
      wifiConnected = false; // WiFi connection failed, but not an error state (no MODE_NO_WIFI_INSTRUCTIONS)
      // Display will just show time from RTC and no weather.
    }
  }

  // Web server (for captive portal) (from server code)
  server.onNotFound([]() {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  });
  server.on("/", HTTP_GET, [](){
    server.send(200, "text/html", "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font-family:sans-serif;text-align:center;padding:20px;}input{width:100%;padding:15px;margin:10px 0;width:90%;}button{background:#000;color:#fff;padding:15px;border:none;width:90%;}</style></head><body><h1>Smart Clock</h1><form method='POST' action='/save'><input name='s' placeholder='WiFi Name'><input name='p' type='password' placeholder='Password'><button>SAVE</button></form></body></html>");
  });
  server.on("/save", HTTP_POST, [](){
    String ssid = server.arg("s");
    String pass = server.arg("p");

    // Show feedback on OLED
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tr);
    drawCenteredStr(20, "Testing WiFi...");
    drawCenteredStr(35, ssid.c_str());
    u8g2.sendBuffer();

    // Attempt to connect
    WiFi.begin(ssid.c_str(), pass.c_str());
    int i = 0;
    while (WiFi.status() != WL_CONNECTED && i < 20) { // ~10 second timeout
      delay(500);
      i++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      // SUCCESS
      u8g2.clearBuffer();
      drawCenteredStr(32, "Success!");
      drawCenteredStr(48, "Restarting...");
      u8g2.sendBuffer();
      
      server.send(200, "text/html", "<h1>Success!</h1><p>Credentials saved. The device will now restart.</p>");
      
      prefs.begin("wifi", false);
      prefs.putString("ssid", ssid);
      prefs.putString("pass", pass);
      prefs.end();
      
      delay(2000);
      ESP.restart();
    } else {
      // FAILURE
      u8g2.clearBuffer();
      drawCenteredStr(20, "Connection Failed!");
      drawCenteredStr(35, "Please try again.");
      u8g2.sendBuffer();

      String html = "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font-family:sans-serif;text-align:center;padding:20px;}input{width:90%;padding:15px;margin:10px 0;}button{background:#000;color:#fff;padding:15px;border:none;width:90%;}p{color:red;}</style></head>";
      html += "<body><h1>Smart Clock</h1><p>Connection Failed!<br>Please check WiFi Name and Password.</p><form method='POST' action='/save'><input name='s' placeholder='WiFi Name' value='" + ssid + "'><input name='p' type='password' placeholder='Password'><button>SAVE</button></form></body></html>";
      server.send(200, "text/html", html);
      WiFi.disconnect(); // Disconnect from the failed attempt
    }
  });
  server.begin();
}

// ==================================================
// MAIN LOOP
// ==================================================
void loop() {
  currentTime = millis();
  updateSmoothBattery();

  // Critical battery voltage cutoff
  if (smoothedBatteryVoltage > 0 && smoothedBatteryVoltage < 3.45) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tr);
    drawCenteredStr(32, "BATTERY EMPTY");
    drawCenteredStr(48, "Shutting down...");
    u8g2.sendBuffer();
    delay(2000); // Let user see the message

    // Go to deep sleep, can only be woken by button
    esp_sleep_enable_ext1_wakeup(1ULL << BUTTON_PIN, ESP_EXT1_WAKEUP_ALL_LOW);
    esp_deep_sleep_start();
  }

  /*
  // ... low battery logic ...
  */

  if (oledIsOffForIdle) { // If OLED was off, turn it back on for any interaction
    u8g2.setPowerSave(0);
    oledIsOffForIdle = false;
  }

  // Slow polling for MPU during idle (OLED off)
  static unsigned long lastMPURead = 0;
  unsigned long mpuInterval = oledIsOffForIdle ? 200 : 20; // 200ms when idle, 20ms when active

  if (mpuOK && millis() - lastMPURead > mpuInterval) {
    lastMPURead = millis();
    mpu.update();
  }
  
  if (currentAppMode == MODE_PORTAL) dnsServer.processNextRequest(); // Adapt currentMode to currentAppMode
  server.handleClient();
  
  // ============= BUTTON HANDLING (Desk services - Adapted from server code) =============
  int btn = digitalRead(BUTTON_PIN);
  if (btn == LOW && !buttonDown) { // Using buttonDown for button state
    buttonDown = true;
    buttonPressStartTime = currentTime;
  }
  if (btn == HIGH && buttonDown) { // Button released
    buttonDown = false;
    unsigned long duration = currentTime - buttonPressStartTime;

    if (duration > longPressThreshold) { // It was a long press
      if (currentAppMode == MODE_STOPWATCH) {
        // Existing stopwatch reset logic
        stopwatchRunning = false;
        stopwatchElapsed = 0;
      } else {
        // General long press -> go to shutdown confirm mode
        currentAppMode = MODE_SHUTDOWN_CONFIRM;
      }
    } else { // It was a short press (less than longPressThreshold)
      if (currentTime - lastReleaseTime < doubleClickGap) {
        onDoubleClick();
        lastReleaseTime = 0;
      } else {
        lastReleaseTime = currentTime;
      }
    }
  }
  // Single click timeout (still applies to short presses)
  if (lastReleaseTime > 0 && (currentTime - lastReleaseTime > doubleClickGap)) {
    onSingleClick();
    lastReleaseTime = 0;
  }

  // ============= TOUCH & MOTION (Ride Buddy) =============
  if (currentAppMode == MODE_EYES || currentAppMode == MODE_CLOCK || currentAppMode == MODE_GAME_FLAPPY || currentAppMode == MODE_GAME_MENU) {
    handleTouchAndMotion(currentTime);
  }

  // Periodic WiFi sync (2 times a day)
  // Check if it's time for a periodic sync (e.g., every 12 hours = 43,200,000 ms)
  // And ensure it's not during captive portal mode
  if (currentAppMode != MODE_PORTAL && (currentTime - lastPeriodicSyncTime > 43200000UL)) { // 12 hours
    lastPeriodicSyncTime = currentTime;

    // Turn WiFi ON
    WiFi.begin();
    unsigned long st = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - st < 10000) { // 10 second timeout for connection
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      configTime(19800, 0, "pool.ntp.org"); // IST

      // Wait for NTP to sync
      struct tm timeinfo;
      int retry = 0;
      while (!getLocalTime(&timeinfo) || timeinfo.tm_year < 100) {
        delay(500);
        if (++retry > 10) break; // Timeout after 5s
      }

      if (timeinfo.tm_year > 100) { // Sync successful
        isTimeSynced = true;
        fetchWeather();
        // Store current epoch time in RTC memory
        time_t now;
        time(&now);
        rtcData.lastEpochTime = now;
      }
      
      // Disconnect WiFi completely to save power
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      btStop();
      wifiConnected = false; // Set flag to false as WiFi is now off
    } else {
      wifiConnected = false; // Sync failed
    }
  }
  
  // ============= MODE RENDERING =============
  static AppMode previousMode = MODE_EYES;
  if (currentAppMode != previousMode) {
    if (currentAppMode == MODE_CLOCK) {
      runClockStartupAnimation = true;
      clockAnimStartTime = millis();
    }
    previousMode = currentAppMode;
  }

  if (currentAppMode == MODE_EYES) {
    eyes.update();               // Animate eyes
    delay(10);
  } else {
    // Desk modes → use u8g2 drawing
    static unsigned long lastRefresh = 0;
    AppMode m = currentAppMode;
    unsigned long interval = 1000; // Default
    if (m == MODE_GAME_RACING) interval = 30;
    else if (m == MODE_GAME_FLAPPY || m == MODE_STOPWATCH || m == MODE_CLOCK || buttonDown) interval = 50;
    
    if (currentTime - lastRefresh >= interval) {
      if (currentAppMode == MODE_CLOCK) updateOLEDClock();
      else if (currentAppMode == MODE_MENU) drawMenu();
      else if (currentAppMode == MODE_STOPWATCH) drawStopwatch();
      else if (currentAppMode == MODE_GAME_MENU) drawGameMenu();
      else if (currentAppMode == MODE_GAME_FLAPPY) drawFlappyGame();
      else if (currentAppMode == MODE_GAME_RACING) drawRacingGame();
      else if (currentAppMode == MODE_PORTAL) drawPortalScreen();
      else if (currentAppMode == MODE_RESET_CONFIRM) drawResetConfirm();
      else if (currentAppMode == MODE_SHUTDOWN_CONFIRM) drawShutdownConfirm();
      lastRefresh = currentTime;
    }
  }

  // --- TEMPORARY BATTERY TEST ---
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    float vb = readBatteryVoltage();
    int pct = batteryPercent(vb);

    Serial.print("Battery: ");
    Serial.print(vb, 2);
    Serial.print(" V  |  ");
    Serial.print(pct);
    Serial.println(" %");

    lastPrint = millis();
  }
}

// ==================================================
// RIDE BUDDY LOGIC (100% original - only minor refactor) 
// ==================================================
void handleTouchAndMotion(unsigned long currentTime) {
  if (oledIsOffForIdle) { // If OLED was off, turn it back on
    u8g2.setPowerSave(0);
    oledIsOffForIdle = false;
  }
  // TOUCH (unchanged)
  int currentState = digitalRead(TOUCH_PIN);
  if (currentState != lastFlickerableState) {
    lastDebounceTime = currentTime;
    lastFlickerableState = currentState;
  }
  if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (lastSteadyState != currentState) {
      lastSteadyState = currentState;
      if (currentState == HIGH) {
        isTouching = true;
        
        // --- GAME MENU SELECTION ---
        if (currentAppMode == MODE_GAME_MENU) {
           if (gameMenuSelection == 0) {
             currentAppMode = MODE_GAME_FLAPPY;
             birdY = 32; birdV = 0; pipeX = 128; pipeGapY = 30; score = 0; isGameOver = false; gameStarted = false;
           } else {
             currentAppMode = MODE_GAME_RACING;
             raceScore = 0; raceSpeed = 2.0; isGameOver = false;
             for(int i=0; i<3; i++) raceObstacles[i].active = false;
           }
           return; // Skip emotion logic
        }
        // ---------------------------

        touchStartTime = currentTime;
        stopSequenceActive = false;
        if (isSleeping) {
          isSleeping = false;
          setEmotion("SCARED");
          ignoreCurrentTouch = true;
          lastInteractionTime = currentTime;
        }
      }
      if (currentState == LOW) {
        isTouching = false;
        unsigned long duration = currentTime - touchStartTime;
        if (ignoreCurrentTouch) {
          ignoreCurrentTouch = false;
          setEmotion("NEUTRAL");
        } else if (duration < HOLD_TIME) {
          tapCount++;
          lastInteractionTime = currentTime;
          if (tapCount == 1) setEmotion("HAPPY");
          else if (tapCount == 2 || tapCount == 3) setEmotion("LOVE");
          else if (tapCount >= 4 && tapCount < 7) setEmotion("ANGRY");
          else if (tapCount >= 7) setEmotion("CRY");
        }
      }
    }
  }

  if (currentState == HIGH && isTouching && !ignoreCurrentTouch) {
    if (currentTime - touchStartTime > HOLD_TIME) {
      setEmotion("SHY");
      tapCount = 0;
      lastInteractionTime = currentTime;
    }
  }

  if (currentTime - lastInteractionTime > 2000 && tapCount > 0) {
    tapCount = 0;
    if (!isSleeping && !isMoving && !stopSequenceActive) setEmotion("NEUTRAL");
  }

  // MOTION
  if (!isTouching && mpuOK) { // Only run motion logic if MPU is OK
    float accX = mpu.getAccX();
    float accY = mpu.getAccY();
    float accZ = mpu.getAccZ();
    float diff = abs(accX - lastX) + abs(accY - lastY) + abs(accZ - lastZ);
    lastX = accX; lastY = accY; lastZ = accZ;

    bool rawMovement = (diff > MOVEMENT_THRESHOLD);
    if (rawMovement) lastMotionTriggerTime = currentTime;
    bool smoothMoving = (currentTime - lastMotionTriggerTime < 300);

    if (smoothMoving) {
      if (currentAppMode == MODE_EYES || currentAppMode == MODE_CLOCK) lastInteractionTime = currentTime;
      stopSequenceActive = false;
      if (!isMoving) {
        isMoving = true;
        isDriving = false;
        motionStartTime = currentTime;
        setEmotion("SCARED");
      } else {
        unsigned long moveDuration = currentTime - motionStartTime;
        if (moveDuration > 3000) {
          isDriving = true;
          if (moveDuration < 18000) setEmotion("DRIVING");
          else setEmotion("NEUTRAL");
        } else {
          setEmotion("SCARED");
        }
      }
    } else {
      if (isMoving) {
        isMoving = false;
        isDriving = false;
        stopSequenceActive = true;
        stopSequenceStartTime = currentTime;
      }
    }
  }

  // Landing sequence
  if (stopSequenceActive && !isTouching) {
    unsigned long seqTime = currentTime - stopSequenceStartTime;
    if (seqTime < 2000) setEmotion("SCARED");
    else if (seqTime < 4000) setEmotion("DISTRACTED");
    else {
      stopSequenceActive = false;
      setEmotion("NEUTRAL");
      lastInteractionTime = currentTime;
    }
  }

  // Idle / Sleep
  else if ((currentAppMode == MODE_EYES || currentAppMode == MODE_CLOCK) && !isTouching && !isMoving && !stopSequenceActive) {
    unsigned long idleDur = currentTime - lastInteractionTime;
    if (idleDur > SLEEP_TIME) { // This is where it transitions to SLEEP emotion
      if (!isSleeping) {
        setEmotion("SLEEP");
        isSleeping = true;
        sleepEmotionDisplayTime = currentTime; // Store time when sleep emotion starts
      }
      // If already sleeping and duration passed, then turn off OLED
      else if (currentTime - sleepEmotionDisplayTime > SLEEP_EMOTION_DURATION) {
        if (!oledIsOffForIdle) { // Only turn off if not already off
          u8g2.clearBuffer();
          u8g2.sendBuffer();
          u8g2.setPowerSave(1); // Turn off OLED display
          oledIsOffForIdle = true;
        }
        // ESP32 remains running, just OLED is off
      }
    } else if (idleDur > BOREDOM_TIME) {
      setEmotion("DISTRACTED");
    } else if (idleDur > 2000 && currentMood != "NEUTRAL") {
      setEmotion("NEUTRAL");
    }
  }
}

void setEmotion(String newMood) {
  if (currentMood != newMood) {
    currentMood = newMood;
    if (newMood == "NEUTRAL") eyes.neutral();
    else if (newMood == "HAPPY") eyes.happy();
    else if (newMood == "LOVE") eyes.love();
    else if (newMood == "ANGRY") eyes.angry();
    else if (newMood == "CRY") eyes.cry();
    else if (newMood == "SHY") eyes.shy();
    else if (newMood == "SLEEP") eyes.sleep();
    else if (newMood == "DISTRACTED") eyes.distracted();
    else if (newMood == "DRIVING") eyes.driving();
    else if (newMood == "SCARED") eyes.scared();
    else if (newMood == "BATTERY") eyes.battery(); // New emotion for low battery

    // When we change emotion → go back to eyes mode (unless already in service mode)
    if (currentAppMode != MODE_PORTAL && currentAppMode != MODE_GAME_FLAPPY && currentAppMode != MODE_GAME_MENU) { 
      // If we are in CLOCK mode, ignore idle mood changes so the clock isn't interrupted
      if (currentAppMode == MODE_CLOCK && (newMood == "NEUTRAL" || newMood == "DISTRACTED" || newMood == "SLEEP")) {
         // Stay in Clock mode
      } else {
         currentAppMode = MODE_EYES;
      }
    }
  }
}

// ==================================================
// BATTERY FUNCTIONS
// ==================================================

// Non-blocking function to update the smoothed battery voltage
void updateSmoothBattery() {
  if (millis() - lastBatteryReadTime > 10000) { // Take a reading every 10 seconds
    lastBatteryReadTime = millis();

    // Initial fill of the array
    if (batteryReadings[9] == 0) {
      for(int i=0; i<10; i++) batteryReadings[i] = readBatteryVoltage();
    }

    // Take a new reading and add it to the buffer
    batteryReadings[readingIndex] = readBatteryVoltage();
    readingIndex++;
    if (readingIndex >= 10) {
      readingIndex = 0;
    }

    // Calculate the new average
    float total = 0;
    for (int i = 0; i < 10; i++) {
      total += batteryReadings[i];
    }
    smoothedBatteryVoltage = total / 10.0;
  }
}

// Returns battery voltage in Volts
float readBatteryVoltage() {
  int mv = analogReadMilliVolts(BATTERY_PIN);  // calibrated
  float v_adc = mv / 1000.0;
  float v_batt = v_adc * 2.0;  // 47k + 47k divider
  return v_batt;
}

// Returns battery percentage (0-100) using a non-linear mapping
int batteryPercent(float v) {
  if (v >= 4.20) return 100;
  if (v >= 4.10) return 90;
  if (v >= 4.00) return 80;
  if (v >= 3.90) return 65;
  if (v >= 3.80) return 50;
  if (v >= 3.70) return 35;
  if (v >= 3.60) return 20;
  if (v >= 3.50) return 10;
  if (v >= 3.40) return 5;
  return 0;
}

// Returns true if battery is charging
bool isCharging() {
  return digitalRead(CHARGING_STATUS_PIN) == CHARGING_STATUS_PIN_LOGIC;
} // ==================================================
// DESK BUDDY FUNCTIONS (mostly unchanged)
// ==================================================
void drawCenteredStr(int y, const char* str) {
  int w = u8g2.getStrWidth(str);
  u8g2.drawStr((128 - w) / 2, y, str);
}

void setupWebServer() {
  server.onNotFound([]() {
    server.sendHeader("Location", "http://" + apIP.toString(), true);
    server.send(302, "text/plain", "");
  });
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html",
      "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font-family:sans-serif;text-align:center;padding:20px;}input{width:90%;padding:15px;margin:10px 0;}button{background:#000;color:#fff;padding:15px;border:none;width:90%;}</style></head>"
      "<body><h1>Ride+Desk Buddy</h1><form method='POST' action='/save'><input name='s' placeholder='WiFi Name'><input name='p' type='password' placeholder='Password'><button>SAVE</button></form></body></html>");
  });
  server.on("/save", HTTP_POST, []() {
    prefs.begin("wifi", false);
    prefs.putString("ssid", server.arg("s"));
    prefs.putString("pass", server.arg("p"));
    prefs.end();
    server.send(200, "text/html", "Saved! Restarting...");
    delay(1000);
    ESP.restart();
  });
  server.begin();
  
  // Prime the battery smoothing filter with an initial reading
  float initialV = readBatteryVoltage();
  for (int i = 0; i < 10; i++) batteryReadings[i] = initialV;
  smoothedBatteryVoltage = initialV;
} void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  
  weatherTemp = "--";
  weatherIcon = ' ';
  locationName = "---";

  http.begin("http://wttr.in/?format=%t\n%x\n%l");
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    int firstNewline = payload.indexOf('\n');
    int secondNewline = payload.indexOf('\n', firstNewline + 1);

    if (firstNewline > 0) {
      weatherTemp = payload.substring(0, firstNewline);
      weatherTemp.trim();
      weatherTemp.replace("°C", "C");

      String codeStr;
      if (secondNewline > 0) {
        codeStr = payload.substring(firstNewline + 1, secondNewline);
        locationName = payload.substring(secondNewline + 1);
        locationName.trim();
        int commaIndex = locationName.indexOf(',');
        if (commaIndex > 0) {
          locationName = locationName.substring(0, commaIndex);
        }
      } else {
        codeStr = payload.substring(firstNewline + 1);
      }
      
      codeStr.trim();
      int code = codeStr.toInt();

      // Map WWO code to our icons
      struct tm ti;
      getLocalTime(&ti);
      bool isNight = (ti.tm_hour >= 19 || ti.tm_hour < 6);

      if (code == 113) { // Clear/Sunny
        weatherIcon = isNight ? 70 : 64;
      } else if (code == 116 || code == 119 || code == 122) { // Partly Cloudy, Cloudy, Overcast
        weatherIcon = 65;
      } else if (code >= 263 && code <= 359) { // Rain variants
        weatherIcon = 67;
      } else { // Snow, Fog, other
        weatherIcon = 69;
      }
    }
  }
  http.end();
}

// Helper function to draw the animated WiFi signal strength
void drawWifiIndicator(int x, int y) {
  if (WiFi.status() != WL_CONNECTED) {
    u8g2.setFont(u8g2_font_profont12_tr);
    u8g2.drawStr(x, y, "X");
    return;
  }
  
  long rssi = WiFi.RSSI();
  int bars = 0;
  if (rssi > -60) bars = 3;
  else if (rssi > -75) bars = 2;
  else bars = 1;

  // Manually draw the WiFi icon
  u8g2.drawDisc(x + 4, y, 1);
  if (bars >= 1) u8g2.drawCircle(x + 4, y, 4, U8G2_DRAW_UPPER_RIGHT);
  if (bars >= 2) u8g2.drawCircle(x + 4, y, 7, U8G2_DRAW_UPPER_RIGHT);
  if (bars >= 3) u8g2.drawCircle(x + 4, y, 10, U8G2_DRAW_UPPER_RIGHT);
}

void drawWeatherIcon(int x, int y, char icon_code) {
  switch (icon_code) {
    case 64: // Sun
      u8g2.drawCircle(x + 4, y + 4, 3); // Center circle
      u8g2.drawLine(x+4, y-1, x+4, y+9); // Vertical
      u8g2.drawLine(x-1, y+4, x+9, y+4); // Horizontal
      u8g2.drawLine(x, y, x+8, y+8); // Diagonal
      u8g2.drawLine(x+8, y, x, y+8); // Diagonal
      break;
    case 70: // Moon
      u8g2.drawCircle(x + 5, y + 4, 4);
      u8g2.setDrawColor(0); // Black
      u8g2.drawCircle(x + 3, y + 4, 4);
      u8g2.setDrawColor(1); // White
      break;
    case 65: // Cloud
      u8g2.drawCircle(x + 3, y + 5, 2);
      u8g2.drawCircle(x + 6, y + 3, 3);
      u8g2.drawCircle(x + 9, y + 5, 2);
      u8g2.drawBox(x+3, y+5, 6, 2);
      break;
    case 67: // Rain
      // Cloud
      u8g2.drawCircle(x + 3, y + 3, 2);
      u8g2.drawCircle(x + 6, y + 1, 3);
      u8g2.drawCircle(x + 9, y + 3, 2);
      u8g2.drawBox(x+3, y+3, 6, 2);
      // Rain drops
      u8g2.drawLine(x + 4, y + 6, x + 3, y + 8);
      u8g2.drawLine(x + 7, y + 6, x + 6, y + 8);
      u8g2.drawLine(x + 10, y + 6, x + 9, y + 8);
      break;
    case 69: // Fog/Snow
      // Cloud
      u8g2.drawCircle(x + 3, y + 3, 2);
      u8g2.drawCircle(x + 6, y + 1, 3);
      u8g2.drawCircle(x + 9, y + 3, 2);
      u8g2.drawBox(x+3, y+3, 6, 2);
      // Snow flakes
      u8g2.drawPixel(x + 4, y + 7);
      u8g2.drawPixel(x + 6, y + 8);
      u8g2.drawPixel(x + 8, y + 7);
      break;
    default:
      u8g2.setFont(u8g2_font_profont10_tr);
      u8g2.drawStr(x, y+8, "?");
      break;
  }
} 
// --- Animation State --- 

void drawClockStartupAnimation() {
  unsigned long elapsed = millis() - clockAnimStartTime;

  u8g2.clearBuffer();

  // --- TOP BAR (SKY) ---
  drawWifiIndicator(2, 10);
  int battPct_project = batteryPercent(readBatteryVoltage());
  u8g2.setFont(u8g2_font_profont10_tr);
  char battStr[5];
  sprintf(battStr, "%d%%", battPct_project);
  u8g2.drawStr(90, 10, battStr);
  u8g2.setFont(u8g2_font_open_iconic_all_1x_t);
  char batteryIcon;
  if (isCharging()) { batteryIcon = 79; }
  else {
    if (battPct_project > 95) batteryIcon = 66; else if (battPct_project > 70) batteryIcon = 67;
    else if (battPct_project > 40) batteryIcon = 68; else if (battPct_project > 15) batteryIcon = 69;
    else batteryIcon = 70;
  }
  u8g2.drawGlyph(118, 10, batteryIcon);

  // --- CAR BODY (Static base) ---
  u8g2.drawBox(4, 28, 120, 3); // Rear Deck
  u8g2.drawLine(4, 28, 2, 46); // Left Fender
  u8g2.drawLine(124, 28, 126, 46); // Right Fender
  u8g2.drawBox(2, 46, 124, 3); // Bottom Bumper line
  
  // 1. Taillight Flash Animation (0-500ms)
  if (elapsed < 500) {
    if ((elapsed / 150) % 2 == 0) {
      u8g2.drawDisc(16, 38, 8, U8G2_DRAW_ALL);
      u8g2.drawDisc(112, 38, 8, U8G2_DRAW_ALL);
    }
  } else {
    u8g2.drawDisc(16, 38, 8, U8G2_DRAW_ALL);
    u8g2.setDrawColor(0); u8g2.drawDisc(16, 38, 4, U8G2_DRAW_ALL); u8g2.setDrawColor(1);
    u8g2.drawDisc(112, 38, 8, U8G2_DRAW_ALL);
    u8g2.setDrawColor(0); u8g2.drawDisc(112, 38, 4, U8G2_DRAW_ALL); u8g2.setDrawColor(1);
  }

  // 2. Spoiler Pop-up Animation (500-800ms)
  int spoiler_y = (elapsed < 500) ? 28 : map(elapsed, 500, 800, 28, 15);
  if (elapsed > 800) spoiler_y = 15;
  
  u8g2.drawBox(10, spoiler_y, 108, 4);
  u8g2.drawBox(32, spoiler_y + 4, 4, 28 - (spoiler_y + 4) > 0 ? 28 - (spoiler_y + 4) : 0);
  u8g2.drawBox(92, spoiler_y + 4, 4, 28 - (spoiler_y + 4) > 0 ? 28 - (spoiler_y + 4) : 0);

  u8g2.sendBuffer();

  if (elapsed > 1000) {
    runClockStartupAnimation = false;
  }
}  

void updateOLEDClock() {

  if (runClockStartupAnimation) {

    drawClockStartupAnimation();

    return;

  }

  

  // 1. GET AND FORMAT DATA

  struct tm ti;

  time_t now;

  time(&now);

  localtime_r(&now, &ti); 
  if (ti.tm_year < (2021 - 1900)) {

    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_logisoso16_tf);

    drawCenteredStr(32, "Time not");

    drawCenteredStr(48, "Synced");

    u8g2.sendBuffer();

    return;

  } 
  // Time for Plate (HH:MM:SS)

  char hStr[3], mStr[3], sStr[3];

  int h12 = ti.tm_hour % 12;

  if (h12 == 0) h12 = 12;

  sprintf(hStr, "%02d", h12);

  sprintf(mStr, "%02d", ti.tm_min);

  sprintf(sStr, "%02d", ti.tm_sec);

  String timeOnPlate = String(hStr) + ":" + String(mStr) + ":" + String(sStr);

  

  // Date for Bottom Data

  char dateBuf[12];

  strftime(dateBuf, sizeof(dateBuf), "%a %d %b", &ti);

  for(int i=0; dateBuf[i]; i++) dateBuf[i] = toupper(dateBuf[i]);

  String dateStr_project = String(dateBuf); 
  // Other data

  String tempStr_project = weatherTemp;

  int battPct_project = batteryPercent(readBatteryVoltage());

  String locName_project = locationName;

  if (locName_project.length() > 6) {

    locName_project = locName_project.substring(0, 6);

  } 
  u8g2.clearBuffer(); 
  // --- TOP BAR (SKY) ---

  // WiFi
  drawWifiIndicator(2, 10);
  // Weather
  if (weatherIcon != ' ') {

    drawWeatherIcon(40, 2, weatherIcon);

    u8g2.setFont(u8g2_font_profont10_tr);

    u8g2.drawStr(54, 10, tempStr_project.c_str());

  }

  // Battery

  u8g2.setFont(u8g2_font_profont10_tr);

  char battStr[5];

  sprintf(battStr, "%d%%", battPct_project);

  u8g2.drawStr(90, 10, battStr);

  u8g2.setFont(u8g2_font_open_iconic_all_1x_t);

  char batteryIcon;

  if (isCharging()) { batteryIcon = 79; }

  else {

    if (battPct_project > 95) batteryIcon = 66; else if (battPct_project > 70) batteryIcon = 67;

    else if (battPct_project > 40) batteryIcon = 68; else if (battPct_project > 15) batteryIcon = 69;

    else batteryIcon = 70;

  }

  u8g2.drawGlyph(118, 10, batteryIcon); 
  // --- ANIMATION CALCULATIONS ---

  // Idling Rumble: Bounces 0 or 1 pixel every 100ms (First 3 seconds only)
  int rumble = 0;
  if (millis() - clockAnimStartTime < 3000) {
    rumble = (millis() / 100) % 2;
  }
  // --- EXHAUST SMOKE ---

  // Simple procedural particles based on time

  // Left Pipe (approx x=10), Right Pipe (approx x=118)

  u8g2.setDrawColor(1);

  for (int i = 0; i < 3; i++) {

    // Offset time for each particle so they don't sync

    long t = millis() + (i * 300);

    int cycle = t % 1500; // 1.5s lifecycle

    

    // Only draw if in the visible part of the cycle

    if (cycle < 1000) {

      int y_smoke = 46 + rumble - (cycle / 50); // Rise up (y decreases)

      int size = (cycle / 300); // Grow

      

      // Drift outward

      int drift = (cycle / 100); 

      

      // Left Puff

      if (y_smoke > 20) u8g2.drawDisc(8 - (drift/2), y_smoke, size > 3 ? 3 : size);

      

      // Right Puff

      if (y_smoke > 20) u8g2.drawDisc(120 + (drift/2), y_smoke, size > 3 ? 3 : size);

    }

  } 
  // --- CAR (Shrunken Height) ---

  // Apply 'rumble' offset to all Y coordinates 
  // Spoiler and Body

  u8g2.drawBox(10, 15 + rumble, 108, 4); // Spoiler

  u8g2.drawBox(6, 17 + rumble, 4, 4);    // Left tip

  u8g2.drawBox(118, 17 + rumble, 4, 4);  // Right tip

  u8g2.drawBox(32, 19 + rumble, 4, 10);  // Left Support
  u8g2.drawBox(92, 19 + rumble, 4, 10);  // Right Support

  

  u8g2.drawBox(4, 28 + rumble, 120, 3); // Rear Deck

  u8g2.drawLine(4, 28 + rumble, 2, 46 + rumble); // Left Fender

  u8g2.drawLine(124, 28 + rumble, 126, 46 + rumble); // Right Fender

  u8g2.drawBox(2, 46 + rumble, 124, 3); // Bottom Bumper line 
  // Pulsing Taillights

  int inner_radius = 4 + 2 * sin(millis() / 400.0);

  

  // Left Light

  u8g2.drawDisc(16, 38 + rumble, 8, U8G2_DRAW_ALL); // Outer ring

  u8g2.setDrawColor(0); u8g2.drawDisc(16, 38 + rumble, inner_radius, U8G2_DRAW_ALL); u8g2.setDrawColor(1); // Inner hole

  

  // Right Light

  u8g2.drawDisc(112, 38 + rumble, 8, U8G2_DRAW_ALL); // Outer ring

  u8g2.setDrawColor(0); u8g2.drawDisc(112, 38 + rumble, inner_radius, U8G2_DRAW_ALL); u8g2.setDrawColor(1); // Inner hole 
  // License Plate with TIME

  u8g2.drawBox(28, 30 + rumble, 72, 15);

  u8g2.setDrawColor(0);

  u8g2.setFont(u8g2_font_profont12_tf); // Compact font for HH:MM:SS

  int timeWidthOnPlate = u8g2.getStrWidth(timeOnPlate.c_str());

  u8g2.drawStr(28 + (72 - timeWidthOnPlate) / 2, 42 + rumble, timeOnPlate.c_str());

  u8g2.setDrawColor(1); 
  // License Plate Glare Animation

  long anim_time = millis() % 4000;

  if (anim_time < 500) { // Animate for 500ms every 4 seconds

    int plate_x = 28;

    int plate_y = 30 + rumble;

    int plate_w = 72;

    int plate_h = 15;

    

    int glare_x = map(anim_time, 0, 500, plate_x - plate_h, plate_x + plate_w); 
    u8g2.setDrawColor(1); // White for the glare

    u8g2.setClipWindow(plate_x, plate_y, plate_x + plate_w, plate_y + plate_h);

    u8g2.drawLine(glare_x, plate_y, glare_x - plate_h, plate_y + plate_h);

    u8g2.setMaxClipWindow();

  } 
  // --- BOTTOM DATA ---

  u8g2.setFont(u8g2_font_profont10_tf);

  

  // Date (Left)

  u8g2.drawStr(4, 62, dateStr_project.c_str());

  

  // Location (Right)

  int locWidth = u8g2.getStrWidth(locName_project.c_str());

  u8g2.drawStr(124 - locWidth, 62, locName_project.c_str());

  

  u8g2.sendBuffer();

} void drawMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  drawCenteredStr(10, "SETTINGS");
  u8g2.drawHLine(0, 12, 128);

  // This logic shows 2 items at a time and scrolls.
  // When item 1 or 2 is selected, offset is 0.
  // When item 3 is selected, offset is -22, showing items 2 and 3.
  // When item 4 is selected, offset is -44, showing items 3 and 4.
  int offset = 0;
  if (menuSelection > 2) {
    offset = (menuSelection - 2) * -22;
  }

  struct MenuItem {
    char icon;
    const char* name;
  };

  // Icons from u8g2_font_open_iconic_all_2x_t
  // 68 = clock, 114 = timer, 73 = lightbulb, 88 = warning, 120 = controller
  MenuItem items[] = {
    {68,  "Home"},
    {114, "Stopwatch"},
    {120, "Games"},
    {73,  "Brightness"},
    {88,  "Reset"}
  };

  for(int i=0; i<5; i++) {
    int y = 16 + (i * 22) + offset;
    // Skip drawing if the item is scrolled off-screen
    if (y < 12 || y > 64) continue;

    if (menuSelection == i + 1) {
      u8g2.drawRBox(3, y, 122, 20, 3);
      u8g2.setDrawColor(0); // Invert color for selected item
    }

    // Draw Icon
    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
    u8g2.drawGlyph(8, y + 17, items[i].icon);

    // Draw Text
    u8g2.setFont(u8g2_font_7x14_tf);
    u8g2.drawStr(32, y + 15, items[i].name);

    // Reset color after drawing an inverted item
    if (menuSelection == i + 1) {
      u8g2.setDrawColor(1);
    }
  }
  u8g2.sendBuffer();
}

void drawStopwatch() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14_tf);
  drawCenteredStr(15, "STOPWATCH");
  u8g2.drawHLine(0, 18, 128);

  unsigned long cur = stopwatchRunning ? (millis() - stopwatchStart) : stopwatchElapsed;
  int m = (cur / 60000) % 60;
  int s = (cur / 1000) % 60;
  int ms = (cur % 1000) / 10;
  char buf[20];
  sprintf(buf, "%02d:%02d.%02d", m, s, ms);
  u8g2.setFont(u8g2_font_logisoso24_tn);
  drawCenteredStr(55, buf); // Centered
  if (buttonDown && currentAppMode == MODE_STOPWATCH) u8g2.drawBox(0, 62, map(millis() - buttonPressStartTime, 0, 2000, 0, 128), 2);
  u8g2.sendBuffer();
}

void drawPortalScreen() {
  u8g2.clearBuffer();
  u8g2.drawRFrame(0, 0, 128, 64, 4);
  u8g2.setFont(u8g2_font_6x12_tr);
  drawCenteredStr(15, "WELCOME");
  u8g2.drawHLine(20, 18, 88);
  
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(8, 30, "1. Connect Phone to WiFi:"); // Moved up
  u8g2.setFont(u8g2_font_6x10_tr);
  drawCenteredStr(42, SETUP_SSID); // Moved up
  
  u8g2.setFont(u8g2_font_5x7_tr);
  drawCenteredStr(54, "2. Open any website"); // Split into two lines
  drawCenteredStr(61, "in Chrome");
  
  if (!isFirstSetup) {
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(95, 10, "EXIT >"); 
  }
  u8g2.sendBuffer();
} void drawResetConfirm() {
  u8g2.clearBuffer();
  u8g2.drawRFrame(2, 5, 124, 54, 3);
  u8g2.setFont(u8g2_font_6x12_tr);
  drawCenteredStr(22, "FACTORY RESET?");
  u8g2.setFont(u8g2_font_5x7_tr);
  drawCenteredStr(35, "THIS WILL ERASE ALL DATA");
  u8g2.drawHLine(15, 40, 98);
  u8g2.setFont(u8g2_font_5x7_tr);
  drawCenteredStr(50, "DBL-CLICK TO CONFIRM");
  drawCenteredStr(59, "SINGLE CLICK TO CANCEL");
  u8g2.sendBuffer();
}

void drawShutdownConfirm() {
  u8g2.clearBuffer();
  u8g2.drawRFrame(2, 5, 124, 54, 3);
  u8g2.setFont(u8g2_font_6x12_tr);
  drawCenteredStr(22, "SHUTDOWN DEVICE?");
  u8g2.setFont(u8g2_font_5x7_tr);
  drawCenteredStr(35, "THIS WILL TURN OFF THE ESP");
  u8g2.drawHLine(15, 40, 98);
  u8g2.setFont(u8g2_font_5x7_tr);
  drawCenteredStr(50, "DBL-CLICK TO CONFIRM");
  drawCenteredStr(59, "SINGLE CLICK TO CANCEL");
  u8g2.sendBuffer();
}

void drawGameMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  drawCenteredStr(10, "SELECT GAME");
  u8g2.drawHLine(0, 12, 128);

  // Draw Card based on selection
  u8g2.drawRFrame(10, 20, 108, 40, 4);
  
  if (gameMenuSelection == 0) {
    u8g2.setFont(u8g2_font_open_iconic_play_2x_t);
    u8g2.drawGlyph(20, 48, 74); 
    u8g2.setFont(u8g2_font_9x15_tf);
    u8g2.drawStr(45, 45, "FLAPPY BOT");
  } else {
    u8g2.setFont(u8g2_font_open_iconic_play_2x_t);
    u8g2.drawGlyph(20, 48, 81); // Car icon
    u8g2.setFont(u8g2_font_9x15_tf);
    u8g2.drawStr(45, 45, "TURBO RACE");
  }
  
  // Instructions
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(4, 62, "BTN: Next  |  TOUCH: Play");

  u8g2.sendBuffer();
}

// Bird/Bot Bitmap (16x12)
const unsigned char bird_bits[] U8X8_PROGMEM = {
  0x00, 0x00, 0x7E, 0x00, 0xFF, 0x01, 0xC3, 0x01, 0xFB, 0x3F, 0xC7, 0x7F, 
  0xDB, 0x67, 0xCB, 0x43, 0xDF, 0x01, 0x7E, 0x00, 0x3C, 0x00, 0x00, 0x00
};

void drawFlappyGame() {
  u8g2.clearBuffer();
  
  if (isGameOver) {
    u8g2.drawRFrame(10, 10, 108, 44, 4);
    u8g2.setFont(u8g2_font_6x12_tr);
    drawCenteredStr(25, "GAME OVER");
    char buf[20];
    sprintf(buf, "Score: %d", score);
    drawCenteredStr(38, buf);
    u8g2.setFont(u8g2_font_4x6_tr);
    drawCenteredStr(48, "TOUCH TO RETRY");
    u8g2.sendBuffer();

    if (isTouching) {
      birdY = 32;
      birdV = 0;
      pipeX = 128;
      pipeGapY = 30;
      score = 0;
      isGameOver = false;
      gameStarted = false;
      delay(500); 
    }
    return;
  }

  // Physics
  if (gameStarted) {
    birdY -= birdV; // Move up/down
    birdV -= 0.3;   // Gravity (Reduced for 50ms)
    
    // Cap velocity
    if (birdV < -4) birdV = -4; 
    if (birdV > 4) birdV = 4;

    // Floor/Ceiling Collision
    if (birdY > 60 || birdY < 0) {
      isGameOver = true;
    }

    // Pipe Logic
    pipeX -= 2; // Speed (Slowed down)
    if (pipeX < -12) {
      pipeX = 128;
      pipeGapY = random(20, 45); 
      score++;
    }

    // Collision with Pipe
    if (pipeX < 24 && pipeX > -2) { // Horizontal overlap
       if (birdY < (pipeGapY - 14) || birdY > (pipeGapY + 14)) { // Vertical overlap
          isGameOver = true;
       }
    }
  } else {
    // Hover animation before start
    birdY = 32 + 4 * sin(millis() / 300.0);
    u8g2.setFont(u8g2_font_4x6_tr);
    drawCenteredStr(50, "TOUCH TO FLAP");
  }

  // Input: Flap
  if (isTouching) {
    if (!gameStarted) gameStarted = true;
    birdV = 2.5; // Flap strength (Softer for 50ms)
  }

  // Drawing
  
  // Bird
  u8g2.drawXBM(10, (int)birdY - 6, 16, 12, bird_bits);

  // Pipe (Draw Rectangles)
  int pipeW = 12;
  int gapH = 28;
  
  // Top Pipe
  u8g2.drawBox(pipeX, 0, pipeW, pipeGapY - (gapH/2));
  u8g2.drawFrame(pipeX, 0, pipeW, pipeGapY - (gapH/2)); // Outline
  
  // Bottom Pipe
  u8g2.drawBox(pipeX, pipeGapY + (gapH/2), pipeW, 64 - (pipeGapY + (gapH/2)));
  u8g2.drawFrame(pipeX, pipeGapY + (gapH/2), pipeW, 64 - (pipeGapY + (gapH/2))); // Outline

  // Score
  u8g2.setFont(u8g2_font_profont12_tr);
  char sBuf[10];
  sprintf(sBuf, "%d", score);
  u8g2.drawStr(60, 10, sBuf);

  u8g2.sendBuffer();
}

// Top-Down Car Bitmap (16x16)
const unsigned char top_car_bits[] U8X8_PROGMEM = {
  0x00, 0x00, 0x70, 0x0E, 0xF8, 0x1F, 0xBC, 0x3D, 0xFC, 0x3F, 0xFC, 0x3F, 
  0xFC, 0x3F, 0x7E, 0x7E, 0x7E, 0x7E, 0xFC, 0x3F, 0xFC, 0x3F, 0xFC, 0x3F, 
  0xBC, 0x3D, 0xF8, 0x1F, 0x70, 0x0E, 0x00, 0x00
};

void drawRacingGame() {
  u8g2.clearBuffer();

  if (isGameOver) {
    u8g2.drawRFrame(10, 10, 108, 44, 4);
    u8g2.setFont(u8g2_font_6x12_tr);
    drawCenteredStr(25, "GAME OVER");
    char buf[20];
    sprintf(buf, "Score: %d", raceScore);
    drawCenteredStr(38, buf);
    u8g2.setFont(u8g2_font_4x6_tr);
    drawCenteredStr(48, "TOUCH TO RETRY");
    u8g2.sendBuffer();

    if (isTouching) {
      raceScore = 0;
      raceSpeed = 2.0;
      isGameOver = false;
      for(int i=0; i<3; i++) raceObstacles[i].active = false;
      delay(500); 
    }
    return;
  }

  // --- CONTROLS (TILT) ---
  if (mpuOK) {
    float tilt = mpu.getAccY(); 
    raceCarX += tilt * 15; 
    if (raceCarX < 28) raceCarX = 28;  // Road Left Edge + Buffer
    if (raceCarX > 100) raceCarX = 100; // Road Right Edge - Buffer
  }

  // --- DRAW ROAD (2D Top Down) ---
  // Grass sides
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, 128, 64);
  
  // Road surface (Black center strip)
  u8g2.setDrawColor(0);
  u8g2.drawBox(20, 0, 88, 64); // Road width 88px centered
  
  // Road Markings (White)
  u8g2.setDrawColor(1);
  u8g2.drawLine(20, 0, 20, 64); // Left Border
  u8g2.drawLine(108, 0, 108, 64); // Right Border
  
  // Moving Dashed Center Line
  int stripeOffset = (millis() / 20) % 16;
  for(int y=-16; y<64; y+=16) {
     int yPos = y + stripeOffset;
     if(yPos < 64) u8g2.drawBox(63, yPos, 2, 8);
  }

  // --- OBSTACLES ---
  if (random(0, 100) < 5) { 
    for(int i=0; i<3; i++) {
      if (!raceObstacles[i].active) {
        raceObstacles[i].active = true;
        raceObstacles[i].z = -10; // Use Z as Y-position in 2D (Starts above screen)
        // 3 Lanes: Left(35), Center(64), Right(93)
        int lane = random(0, 3); 
        if (lane == 0) raceObstacles[i].x = 35;
        else if (lane == 1) raceObstacles[i].x = 64;
        else raceObstacles[i].x = 93;
        break;
      }
    }
  }

  for(int i=0; i<3; i++) {
    if (raceObstacles[i].active) {
      raceObstacles[i].z += raceSpeed; // Move DOWN screen

      int obsY = (int)raceObstacles[i].z;
      int obsX = (int)raceObstacles[i].x;
      
      // Draw Rock (2D)
      u8g2.setDrawColor(1); // White rock on black road
      u8g2.drawBox(obsX - 5, obsY, 10, 10);
      u8g2.setDrawColor(0); // Detail
      u8g2.drawPixel(obsX-2, obsY+2);
      u8g2.drawPixel(obsX+2, obsY+5);
      u8g2.setDrawColor(1);

      // Collision Check
      // Car Y is approx 48. Car size 16x16.
      // Obstacle size 10x10.
      if (obsY > 40 && obsY < 60) { // Vertical overlapping car
        if (abs(raceCarX - obsX) < 12) { // Horizontal overlap
          isGameOver = true;
        }
      }

      if (obsY > 64) {
        raceObstacles[i].active = false;
        raceScore++;
        raceSpeed += 0.05; 
      }
    }
  }

  // --- DRAW CAR ---
  u8g2.setDrawColor(1); 
  u8g2.drawXBM((int)raceCarX - 8, 48, 16, 16, top_car_bits);

  // --- HUD ---
  u8g2.setDrawColor(0); // Black box for text bg
  u8g2.drawBox(0, 0, 30, 10);
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_micro_tr);
  u8g2.setCursor(2, 6);
  u8g2.print("SCR:");
  u8g2.print(raceScore);

  u8g2.sendBuffer();
}

void drawModernWiFi(int x, int y) {
  if (WiFi.status() != WL_CONNECTED) { u8g2.setFont(u8g2_font_6x12_tr); u8g2.drawStr(x, y+8, "!"); return; }
  int rssi = WiFi.RSSI();
  u8g2.drawDisc(x + 7, y + 8, 1); 
  if (rssi > -80) u8g2.drawCircle(x + 7, y + 8, 4, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
} // ============= BUTTON ACTIONS =============
void onSingleClick() {
  if (currentAppMode == MODE_MENU) {
    menuSelection++;
    if (menuSelection > 5) menuSelection = 1; // Keep at 5 items for now, Racing is inside Game Menu
  } else if (currentAppMode == MODE_GAME_MENU) {
    // Cycle Selection
    gameMenuSelection++;
    if (gameMenuSelection > 1) gameMenuSelection = 0;
  } else if (currentAppMode == MODE_GAME_FLAPPY) {
    if (isGameOver) {
      // Retry Flappy
      birdY = 32; birdV = 0; pipeX = 128; pipeGapY = 30; score = 0; isGameOver = false; gameStarted = false;
    } else {
      // Flap (if game started)
      if (!gameStarted) gameStarted = true;
      birdV = 2.5;
    }
  } else if (currentAppMode == MODE_GAME_RACING) {
    if (isGameOver) {
      // Retry Racing
      raceScore = 0; raceSpeed = 2.0; isGameOver = false;
      for(int i=0; i<3; i++) raceObstacles[i].active = false;
    }
    // No action for single click while racing (tilt controls)
  } else if (currentAppMode == MODE_STOPWATCH) {
    if (!stopwatchRunning) {
      stopwatchStart = millis() - stopwatchElapsed;
      stopwatchRunning = true;
    } else {
      stopwatchElapsed = millis() - stopwatchStart;
      stopwatchRunning = false;
    }
  } else if (currentAppMode == MODE_RESET_CONFIRM) { // From server code
    currentAppMode = MODE_MENU;
  } else if (currentAppMode == MODE_SHUTDOWN_CONFIRM) { // New: single click cancels shutdown
    currentAppMode = MODE_EYES;
    lastInteractionTime = currentTime; // Reset interaction time
  } else if (currentAppMode == MODE_CLOCK) { // Back to eyes from clock
    currentAppMode = MODE_EYES;
    lastInteractionTime = currentTime; // Reset interaction time
  } else if (currentAppMode == MODE_EYES) { // Always go to clock from eyes
    if (isTimeSynced) {
      // Time is synced, switch to clock
      currentAppMode = MODE_CLOCK;
      lastInteractionTime = currentTime;
    } else {
      // Time is not synced, provide feedback
      setEmotion("ANGRY");
      lastInteractionTime = currentTime; // Reset interaction time to speed up return to neutral
    }
  }
}

void onDoubleClick() {
  if (currentAppMode == MODE_EYES || currentAppMode == MODE_CLOCK) {
    currentAppMode = MODE_MENU;
  } else if (currentAppMode == MODE_MENU) {
    if (menuSelection == 1) { // "Home"
      if (isTimeSynced) {
        currentAppMode = MODE_CLOCK;
      } else {
        currentAppMode = MODE_EYES;
      }
      lastInteractionTime = currentTime; // Reset interaction time
    }
    else if (menuSelection == 2) currentAppMode = MODE_STOPWATCH;
    else if (menuSelection == 3) {
      currentAppMode = MODE_GAME_MENU;
    }
    else if (menuSelection == 4) {
      // Brightness logic
      brightnessLevel = (brightnessLevel == 255) ? 100 : (brightnessLevel == 100 ? 15 : 255);
      u8g2.setContrast(brightnessLevel);
      // Save the new brightness level
      prefs.begin("display", false);
      prefs.putUChar("bright", brightnessLevel);
      prefs.end();
    } else if (menuSelection == 5) {
      currentAppMode = MODE_RESET_CONFIRM;
    }
  } else if (currentAppMode == MODE_STOPWATCH) {
    currentAppMode = MODE_MENU; // Go to menu from stopwatch
    lastInteractionTime = currentTime; // Reset interaction time
  } else if (currentAppMode == MODE_GAME_MENU) {
    currentAppMode = MODE_MENU;
  } else if (currentAppMode == MODE_GAME_FLAPPY) {
    currentAppMode = MODE_GAME_MENU;
  } else if (currentAppMode == MODE_GAME_RACING) {
    currentAppMode = MODE_GAME_MENU;
  } else if (currentAppMode == MODE_PORTAL) {
    if (!isFirstSetup) {
      dnsServer.stop();
      currentAppMode = MODE_MENU;
    }
  } else if (currentAppMode == MODE_RESET_CONFIRM) { // Finalization from server code
    u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x12_tr); drawCenteredStr(35, "WIPING DATA..."); u8g2.sendBuffer();
    prefs.begin("wifi", false); prefs.clear(); prefs.end(); delay(2000); ESP.restart(); 
  } else if (currentAppMode == MODE_SHUTDOWN_CONFIRM) {
    Serial.println("=== SHUTTING DOWN ===");
    
    // Turn off WiFi completely
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    btStop(); // Also disable Bluetooth
    
    // Store current time before sleep
    time_t now;
    time(&now);
    rtcData.lastEpochTime = now;
    
    // Clear and turn off display
    u8g2.clearBuffer();
    u8g2.sendBuffer();
    u8g2.setPowerSave(1);
    
    // **CRITICAL: Enable button wakeup on GPIO25 using EXT1**
    // EXT1 is more robust and can handle internal pullups during sleep
    esp_sleep_enable_ext1_wakeup(1ULL << BUTTON_PIN, ESP_EXT1_WAKEUP_ALL_LOW);
    
    Serial.println("Press button to power ON");
    Serial.flush();
    delay(100);
    
    // Enter deep sleep - will wake ONLY on button press
    esp_deep_sleep_start();
}
}