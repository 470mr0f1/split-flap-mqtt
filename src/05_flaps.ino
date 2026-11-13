#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ESP8266WebServer.h>
#include "declarations.h"

const uint8_t I2C_MAX_RETRIES = 3;
const uint16_t I2C_TIMEOUT_MS = 100;

// Track I2C errors per unit for health metrics.
uint16_t i2cErrors[MAX_NUMBER_UNITS] = {0};

//checks for new message to show (legacy, instant animation)
void showNewData(char message[number_units+1]) {
  if (strcmp(writtenLast, message) != 0) {
    strncpy(writtenLast, message, number_units);
    writtenLast[number_units] = '\0';
    showMessage(message, flap_speed);
  }
}

// Show new data with animation support
void showNewDataAnimated(char message[number_units+1], AnimationType anim, uint8_t speed) {
  // While a unit reports a failed homing it dropped its last request, so the
  // same text must be sendable again (acts as a retry).
  if (strcmp(writtenLast, message) != 0 || getUnitFaultMask() != 0) {
    strncpy(writtenLast, message, number_units);
    writtenLast[number_units] = '\0';
    showMessageAnimated(message, speed, anim);
  }
}

//pushes message to units (instant - all at once)
void showMessage(char message[number_units+1], int flapSpeed) {
  waitForDisplayIdle();
  Serial.println("start to send (instant)");
  for (int i = 0; i < number_units; i++) {
    char currentLetter = toupper(message[i]);
    int currentLetterPosition = translateLettertoInt(currentLetter);
    writeToUnit(i, currentLetterPosition, flapSpeed);
  }
}

// Show message with animation
void showMessageAnimated(char message[number_units+1], int flapSpeed, AnimationType anim) {
  if (number_units > MAX_NUMBER_UNITS) {
    Serial.println("Animation aborted: number_units exceeds MAX_NUMBER_UNITS");
    return;
  }

  initializeAnimationRandom();
  waitForDisplayIdle();

  switch (anim) {
    case ANIM_WAVE:
      showWaveAnimation(message, flapSpeed);
      break;

    case ANIM_SCROLL:
      showScrollAnimation(message, flapSpeed);
      break;

    case ANIM_RANDOM:
      showRandomAnimation(message, flapSpeed);
      break;

    case ANIM_SLOT_MACHINE:
      showSlotMachineAnimation(message, flapSpeed);
      break;

    case ANIM_SCRAMBLE:
      showScrambleAnimation(message, flapSpeed);
      break;

    case ANIM_CENTER_OUT:
      showCenterOutAnimation(message, flapSpeed);
      break;

    case ANIM_TYPEWRITER:
      showTypewriterAnimation(message, flapSpeed);
      break;

    case ANIM_MATRIX:
      showMatrixAnimation(message, flapSpeed);
      break;

     case ANIM_COUNTDOWN:
       showCountdownAnimation(message, flapSpeed);
       break;

     case ANIM_CASCADE:
       showCascadeAnimation(message, flapSpeed);
       break;

     case ANIM_SPIN:
       showSpinAnimation(message, flapSpeed);
       break;

     case ANIM_GLITCH:
       showGlitchAnimation(message, flapSpeed);
       break;

     case ANIM_WIPE:
       showWipeAnimation(message, flapSpeed);
       break;

    case ANIM_INSTANT:
    default:
      showMessage(message, flapSpeed);
      break;
  }
}

// Initialize random generator only once
void initializeAnimationRandom() {
  static bool initialized = false;

  if (!initialized) {
    randomSeed(micros());
    initialized = true;
  }
}

// Return a random flap position, optionally excluding one position
uint8_t getRandomFlapPosition(uint8_t excludedPosition) {
  if (flapamount <= 1) {
    return 0;
  }

  // Select from all positions except the excluded one
  uint8_t position = random(0, flapamount - 1);

  if (position >= excludedPosition) {
    position++;
  }

  return position;
}

// Convert message character to flap position
uint8_t getMessagePosition(char character) {
  return translateLettertoInt(toupper(character));
}

// Wait for display to be idle with timeout
void waitForDisplayIdle() {
  unsigned long startWait = millis();
  while (isDisplayMoving()) {
    Serial.println("moving");
    delay(500);
    ESP.wdtFeed();
    if (millis() - startWait > 30000) {
      Serial.println("Display wait timeout, proceeding anyway");
      break;
    }
  }
}

// Wave animation: left to right with delay between units
void showWaveAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (wave)");
  for (int i = 0; i < number_units; i++) {
    char currentLetter = toupper(message[i]);
    int currentLetterPosition = translateLettertoInt(currentLetter);
    writeToUnit(i, currentLetterPosition, flapSpeed);
    delay(100);  // Delay between each unit for wave effect
    ESP.wdtFeed();
  }
}

// Scroll animation: characters appear from right, scroll left
void showScrollAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (scroll)");
  char tempMsg[MAX_NUMBER_UNITS + 1];

  // Start with all spaces
  for (int i = 0; i < number_units; i++) {
    tempMsg[i] = ' ';
  }
  tempMsg[number_units] = '\0';

  // Scroll each character in from the right
  for (int charIdx = 0; charIdx < number_units; charIdx++) {
    // Shift everything left
    for (int i = 0; i < number_units - 1; i++) {
      tempMsg[i] = tempMsg[i + 1];
    }
    // Add new character on the right
    tempMsg[number_units - 1] = message[charIdx];

    // Update display
    for (int i = 0; i < number_units; i++) {
      char currentLetter = toupper(tempMsg[i]);
      int currentLetterPosition = translateLettertoInt(currentLetter);
      writeToUnit(i, currentLetterPosition, flapSpeed);
    }

    // Wait for movement and add delay
    delay(200);
    ESP.wdtFeed();
    waitForDisplayIdle();
  }
}

// Random animation: reveal characters in random order
void showRandomAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (random)");

  // Track which units have been revealed
  bool revealed[MAX_NUMBER_UNITS] = {false};
  int remaining = number_units;

  // First, set all to space
  for (int i = 0; i < number_units; i++) {
    writeToUnit(i, 0, flapSpeed);  // 0 = space
  }
  delay(200);
  ESP.wdtFeed();

  // Reveal in random order
  while (remaining > 0) {
    // Pick a random unrevealed unit
    int pick = random(remaining);
    int unitIdx = -1;
    int count = 0;

    for (int i = 0; i < number_units; i++) {
      if (!revealed[i]) {
        if (count == pick) {
          unitIdx = i;
          break;
        }
        count++;
      }
    }

    if (unitIdx >= 0) {
      char currentLetter = toupper(message[unitIdx]);
      int currentLetterPosition = translateLettertoInt(currentLetter);
      writeToUnit(unitIdx, currentLetterPosition, flapSpeed);
      revealed[unitIdx] = true;
      remaining--;
      delay(150);
      ESP.wdtFeed();
    }
  }
}

void showSlotMachineAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (slot machine)");

  initializeAnimationRandom();

  uint8_t target[MAX_NUMBER_UNITS];

  // Determine target positions
  for (int i = 0; i < number_units; i++) {
    target[i] = getMessagePosition(message[i]);
  }

  // Random spinning phase
  const uint8_t rounds = 10;

  for (uint8_t round = 0; round < rounds; round++) {

    for (int i = 0; i < number_units; i++) {
      uint8_t randomPosition = getRandomFlapPosition(target[i]);
      writeToUnit(i, randomPosition, flapSpeed);
    }

    waitForDisplayIdle();
    delay(50);
    ESP.wdtFeed();
  }

  // Stop modules one after another, from left to right
  for (int i = 0; i < number_units; i++) {
    writeToUnit(i, target[i], flapSpeed);

    waitForDisplayIdle();

    delay(100);
    ESP.wdtFeed();
  }
}

void showScrambleAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (scramble)");

  initializeAnimationRandom();

  uint8_t target[MAX_NUMBER_UNITS];
  bool settled[MAX_NUMBER_UNITS] = {false};

  // Determine target positions
  for (int i = 0; i < number_units; i++) {
    target[i] = getMessagePosition(message[i]);
  }

  // Randomize all modules repeatedly
  const uint8_t rounds = 8;

  for (uint8_t round = 0; round < rounds; round++) {

    for (int i = 0; i < number_units; i++) {
      uint8_t randomPosition = getRandomFlapPosition(target[i]);
      writeToUnit(i, randomPosition, flapSpeed);
    }

    waitForDisplayIdle();

    delay(80);
    ESP.wdtFeed();
  }

  // Reveal target characters in random order
  int remaining = number_units;

  while (remaining > 0) {

    int pick = random(remaining);
    int unitIdx = -1;
    int count = 0;

    for (int i = 0; i < number_units; i++) {
      if (!settled[i]) {
        if (count == pick) {
          unitIdx = i;
          break;
        }
        count++;
      }
    }

    if (unitIdx >= 0) {
      writeToUnit(unitIdx, target[unitIdx], flapSpeed);

      waitForDisplayIdle();

      settled[unitIdx] = true;
      remaining--;

      delay(100);
      ESP.wdtFeed();
    }
  }
}

void showCenterOutAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (center out)");

  int left = (number_units - 1) / 2;
  int right = number_units / 2;

  // Start in the center and expand outwards
  while (left >= 0 || right < number_units) {

    if (left == right) {
      // Odd number of modules: exactly one center module
      uint8_t position = getMessagePosition(message[left]);
      writeToUnit(left, position, flapSpeed);

      left--;
      right++;
    }
    else {
      // Even number of modules or outer positions
      if (left >= 0) {
        uint8_t position = getMessagePosition(message[left]);
        writeToUnit(left, position, flapSpeed);
        left--;
      }

      if (right < number_units) {
        uint8_t position = getMessagePosition(message[right]);
        writeToUnit(right, position, flapSpeed);
        right++;
      }
    }

    delay(120);
    ESP.wdtFeed();
  }

  waitForDisplayIdle();
}

void showTypewriterAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (typewriter)");

  for (int i = 0; i < number_units; i++) {

    uint8_t position = getMessagePosition(message[i]);

    writeToUnit(i, position, flapSpeed);

    // Wait until the current character has finished moving
    waitForDisplayIdle();

    delay(120);
    ESP.wdtFeed();
  }
}

void showMatrixAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (matrix)");

  initializeAnimationRandom();

  uint8_t target[MAX_NUMBER_UNITS];

  // Determine target positions
  for (int i = 0; i < number_units; i++) {
    target[i] = getMessagePosition(message[i]);
  }

  // Matrix effect: random characters on all modules
  const uint8_t rounds = 12;

  for (uint8_t round = 0; round < rounds; round++) {

    for (int i = 0; i < number_units; i++) {
      uint8_t randomPosition = random(0, flapamount);
      writeToUnit(i, randomPosition, flapSpeed);

      // Slight delay between modules creates a flowing effect
      delay(25);
    }

    waitForDisplayIdle();

    delay(40);
    ESP.wdtFeed();
  }

  // Resolve the message from left to right
  for (int i = 0; i < number_units; i++) {

    writeToUnit(i, target[i], flapSpeed);

    waitForDisplayIdle();

    delay(80);
    ESP.wdtFeed();
  }
}

// -----------------------------------------------------------------------------
// Additional animations
// -----------------------------------------------------------------------------

// Countdown: all modules cycle through random characters. The pauses become
// progressively longer, then the final message is revealed from left to right.
void showCountdownAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (countdown)");

  const uint8_t rounds = 10;

  for (uint8_t round = 0; round < rounds; round++) {
    for (int i = 0; i < number_units; i++) {
      uint8_t position = random(0, flapamount);
      writeToUnit(i, position, flapSpeed);
    }

    waitForDisplayIdle();
    delay(20 + round * 25);
    ESP.wdtFeed();
  }

  for (int i = 0; i < number_units; i++) {
    writeToUnit(i, getMessagePosition(message[i]), flapSpeed);
    waitForDisplayIdle();
    delay(80);
    ESP.wdtFeed();
  }
}

// Cascade: each module rapidly changes through random characters and then
// settles on its target before the next module starts.
void showCascadeAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (cascade)");

  const uint8_t rounds = 6;

  for (int i = 0; i < number_units; i++) {
    for (uint8_t round = 0; round < rounds; round++) {
      writeToUnit(i, random(0, flapamount), flapSpeed);
      waitForDisplayIdle();
      delay(35);
      ESP.wdtFeed();
    }

    writeToUnit(i, getMessagePosition(message[i]), flapSpeed);
    waitForDisplayIdle();
    delay(60);
  }
}

// Spin: modules cycle through the available flap positions. A per-module
// offset gives the display a more lively effect. Modules then stop in order.
void showSpinAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (spin)");

  if (flapamount == 0) return;

  const uint8_t rotations = 2;
  const uint16_t totalSteps = (uint16_t)flapamount * rotations;

  for (uint16_t step = 0; step < totalSteps; step++) {
    for (int i = 0; i < number_units; i++) {
      uint8_t position = (step + (i * 3)) % flapamount;
      writeToUnit(i, position, flapSpeed);
    }

    waitForDisplayIdle();
    delay(15);
    ESP.wdtFeed();
  }

  for (int i = 0; i < number_units; i++) {
    writeToUnit(i, getMessagePosition(message[i]), flapSpeed);
    waitForDisplayIdle();
    delay(100);
    ESP.wdtFeed();
  }
}

// Glitch: randomly selected modules jump to random characters. The final
// target text is sent to all modules together.
void showGlitchAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (glitch)");

  const uint8_t rounds = 15;

  for (uint8_t round = 0; round < rounds; round++) {
    for (int i = 0; i < number_units; i++) {
      if (random(0, 100) < 55) {
        writeToUnit(i, random(0, flapamount), flapSpeed);
      }
    }

    waitForDisplayIdle();
    delay(random(20, 80));
    ESP.wdtFeed();
  }

  for (int i = 0; i < number_units; i++) {
    writeToUnit(i, getMessagePosition(message[i]), flapSpeed);
  }

  waitForDisplayIdle();
}

// Wipe: clear the complete display, then write the new message from left to right.
void showWipeAnimation(char message[number_units+1], int flapSpeed) {
  Serial.println("start to send (wipe)");

  const uint8_t blankPosition = translateLettertoInt(' ');

  for (int i = 0; i < number_units; i++) {
    writeToUnit(i, blankPosition, flapSpeed);
  }

  waitForDisplayIdle();
  delay(150);

  for (int i = 0; i < number_units; i++) {
    writeToUnit(i, getMessagePosition(message[i]), flapSpeed);
    waitForDisplayIdle();
    delay(70);
    ESP.wdtFeed();
  }
}

//translates char to letter position
uint8_t translateLettertoInt(char letterchar) {
  uint8_t match_char = 0;  // Default to space (position 0) if no match
  for (uint8_t i = 0; i < flapamount; i++) {
    if (letterchar == letters[i]) {
      match_char = i;
      break;
    }
  }
  return match_char;
}

//write letter position and speed in rpm to single unit
void writeToUnit(uint8_t address, uint8_t letter, int flapSpeed) {
  uint8_t sendArray[2] = {letter, (uint8_t)flapSpeed};

  Wire.beginTransmission(address);
  Wire.write(sendArray, sizeof(sendArray));
  Wire.endTransmission();
}

// Write LED brightness and RGB color to single unit
void writeLedToUnit(uint8_t address,
                    uint8_t brightness,
                    uint8_t red,
                    uint8_t green,
                    uint8_t blue) {

  uint8_t sendArray[5] = {
    0xFF,        // LED command
    brightness,  // Brightness 0-255
    red,         // Red 0-255
    green,       // Green 0-255
    blue         // Blue 0-255
  };

  Wire.beginTransmission(address);
  Wire.write(sendArray, sizeof(sendArray));
  Wire.endTransmission();
}

//checks if unit in display is currently moving
bool isDisplayMoving() {
  for (int i = 0; i < number_units; i++) {
    displayState[i] = checkIfMoving(i);
    if (displayState[i] == 1) {
      return true;
    }
  }
  return false;
}

//checks if single unit is moving with retry logic
int checkIfMoving(uint8_t address) {
  int active = -1;

  for (uint8_t retry = 0; retry < I2C_MAX_RETRIES; retry++) {
    unsigned long startTime = millis();

    Wire.requestFrom(address, answersize, (uint8_t)1);

    while (Wire.available() == 0) {
      if (millis() - startTime > I2C_TIMEOUT_MS) {
        break;
      }
    }

    if (Wire.available() > 0) {
      active = Wire.read();
      if (active != -1) {
        return active;
      }
    }

    Wire.beginTransmission(address);
    Wire.endTransmission();
    delay(5);
  }

  if (address < number_units && address < MAX_NUMBER_UNITS) {
    i2cErrors[address]++;
  }
  return 0;
}

// Get total I2C errors for health reporting
uint32_t getTotalI2CErrors() {
  uint32_t total = 0;
  for (int i = 0; i < number_units && i < MAX_NUMBER_UNITS; i++) {
    total += i2cErrors[i];
  }
  return total;
}
