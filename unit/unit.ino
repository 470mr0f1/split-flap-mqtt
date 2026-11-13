/*********
  Split Flap Arduino Nano Unit
*********/

//#define SERIAL_ENABLE // uncomment for serial debug communication
//#define TEST_ENABLE   // uncomment for Test mode. Rotates through a few character to make sure unit is working. These characters should be displayed in the correct order: " ", "Z", "A", "U", "N", "?", "0", "1", "2", "9"

#include <Arduino.h>
#include <Wire.h>
#include <Stepper.h>
#include <EEPROM.h>
#include <avr/sleep.h>
#include <Adafruit_NeoPixel.h>

// Pins of I2C adress switch
#define ADRESSSW1 6
#define ADRESSSW2 5
#define ADRESSSW3 4
#define ADRESSSW4 3

//constants stepper
#define STEPPERPIN1 11
#define STEPPERPIN2 10
#define STEPPERPIN3 9
#define STEPPERPIN4 8
#define STEPS 2038 //28BYJ-48 stepper, number of steps
#define HALLPIN 7 //Pin of hall sensor
#define AMOUNTFLAPS 45

// WS2811B LED
// NOTE: A6/A7 of the Nano are analog-input only (no digital output), so the
// NeoPixel data line must be on a digital-capable pin. D12 is free here.
#define LED_PIN 2
#define LED_COUNT 1
#define LED_COMMAND 0xFF
Adafruit_NeoPixel statusLed(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// I2C receive values; LED updates are applied in loop(), not in the ISR.
volatile uint8_t ledBrightness = 0;
volatile uint8_t ledRed = 0;
volatile uint8_t ledGreen = 0;
volatile uint8_t ledBlue = 0;
volatile bool ledUpdatePending = false;

// Calibration offset received via I2C; EEPROM is written in loop(), not in the ISR.
volatile uint16_t pendingCalOffset = 0;
volatile bool offsetWritePending = false;

//constants others
#define BAUDRATE 115200
#define ROTATIONDIRECTION -1 //-1 for reverse direction
#define MIN_STEPPER_SPEED 1  //rpm limits for speed values received via I2C (0 would divide by zero in Stepper, >~15 skips steps)
#define MAX_STEPPER_SPEED 12
#define CAL_RETRY_TIMEOUT 60 //seconds to wait before homing is retried after a failed calibration
#define OVERHEATINGTIMEOUT 2 //timeout in seconds to avoid overheating of stepper. After starting rotation, the counter will start. Stepper won't move again until timeout is passed
unsigned long lastRotation = 0;

#ifdef SERIAL_ENABLE
//Serial input stuff
const byte numChars = 32;
char receivedChars[numChars]; // an array to store the received data
boolean newData = false;
#endif

//globals
volatile uint8_t displayedLetter = 0; //currently shown letter
int desiredLetter = 0; //letter to be shown
const char letters[] = {' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'Ä', 'Ö', 'Ü', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', '.', '-', '?', '!'};
Stepper stepper(STEPS, STEPPERPIN1, STEPPERPIN3, STEPPERPIN2, STEPPERPIN4); //stepper setup
bool lastInd1 = false; //store last status of phase
bool lastInd2 = false; //store last status of phase
bool lastInd3 = false; //store last status of phase
bool lastInd4 = false; //store last status of phase
volatile uint8_t currentlyrotating = 0; // 1 = drum is currently rotating, 0 = drum is standing still
volatile bool calibrationFailed = false; // set when homing failed, reported to the master as status 2
unsigned long calibrationFailedAt = 0;
volatile uint8_t stepperSpeed = 10; //current speed of stepper, value only for first homing
int eeAddress = 0;   //EEPROM address for calibration offset
uint16_t calOffset;       //Offset for calibration in steps, stored in EEPROM, gets read in setup
volatile uint8_t receivedNumber = 0;
int i2cAddress;

//sleep globals
const unsigned long WAIT_TIME = 2000;    //wait time before sleep routine gets executed again in milliseconds
unsigned long previousMillis = 0;       //stores last time sleep was interrupted

//test calibration settings
#if defined(SERIAL_ENABLE) || defined(TEST_ENABLE)
int calLetters[10] = {0, 26, 1, 21, 14, 43, 30, 31, 32, 39};
//int calLetters[10] = {0, 6, 1, 5, 0};

void run_test() {
  stepperSpeed = 12;
  for (int i = 0; i < 10; i++) {
    int currentCalLetter = calLetters[i];
    rotateToLetter(currentCalLetter);
    delay(2000);
  }
}
#endif

//setup
void setup() {
  // i2c adress switch
  pinMode(ADRESSSW1, INPUT_PULLUP);
  pinMode(ADRESSSW2, INPUT_PULLUP);
  pinMode(ADRESSSW3, INPUT_PULLUP);
  pinMode(ADRESSSW4, INPUT_PULLUP);

  //hall sensor
  pinMode(HALLPIN, INPUT);

#ifndef SERIAL_ENABLE
  pinMode(LED_BUILTIN, OUTPUT); // used as sleep/wake indicator in loop()
#endif

  // WS2811B initialization
  statusLed.begin();
  statusLed.clear();
  statusLed.show();

  i2cAddress = getaddress(); //get I2C Address and save in variable

#ifdef SERIAL_ENABLE
  //initialize serial
  Serial.begin(BAUDRATE);
  Serial.println("starting unit");
  Serial.print("I2CAddress: ");
  Serial.println(i2cAddress);
#endif

  //I2C function assignment
  Wire.begin(i2cAddress); //i2c address of this unit
  Wire.onReceive(receiveLetter);//call-function for transfered letter via i2c
  Wire.onRequest(requestEvent); //call-funtion if master requests unit state

  getOffset();     //get calibration offset from EEPROM
  calibrate(true); //home stepper after startup

#ifdef TEST_ENABLE
  // Run test on startup
  run_test();
#endif
}

void loop() {
  unsigned long currentMillis = millis();
  // Do not sleep while a move/LED/EEPROM update is still pending
  if (currentMillis - previousMillis >= WAIT_TIME &&
      displayedLetter == receivedNumber &&
      !ledUpdatePending && !offsetWritePending) {
    byte old_ADCSRA = ADCSRA;
    // disable ADC
    ADCSRA = 0;
#ifdef SERIAL_ENABLE
    set_sleep_mode (SLEEP_MODE_IDLE);
#else
    set_sleep_mode (SLEEP_MODE_PWR_DOWN);
#endif
    sleep_enable();
#ifndef SERIAL_ENABLE
    digitalWrite (LED_BUILTIN, LOW); // shuts off LED when starting to sleep, for debugging
#endif
    sleep_cpu ();
#ifndef SERIAL_ENABLE
    digitalWrite (LED_BUILTIN, HIGH); // turns on LED when waking up, for debugging
#endif
    sleep_disable();
    previousMillis = currentMillis; //reset sleep counter
    ADCSRA = old_ADCSRA;

    // release TWI bus
    TWCR = bit(TWEN) | bit(TWIE) | bit(TWEA) | bit(TWINT);

    // turn it back on again
    Wire.begin (i2cAddress);
  }  // end of time to sleep

  // Apply LED changes outside the I2C interrupt routine.
  if (ledUpdatePending) {
    uint8_t brightness;
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    noInterrupts();
    brightness = ledBrightness;
    red = ledRed;
    green = ledGreen;
    blue = ledBlue;
    ledUpdatePending = false;
    interrupts();

    statusLed.setBrightness(brightness);
    statusLed.setPixelColor(0, statusLed.Color(red, green, blue));
    statusLed.show();
  }

  // Store a new calibration offset (slow EEPROM write, must not run in the ISR).
  if (offsetWritePending) {
    uint16_t newOffset;

    noInterrupts();
    newOffset = pendingCalOffset;
    offsetWritePending = false;
    interrupts();

    if (newOffset != calOffset) {
      calOffset = newOffset;
      writeToEEPROM(calOffset);
    }
  }

  //check if new letter was received through i2c
  if (displayedLetter != receivedNumber)
  {
    /*
      #ifdef SERIAL_ENABLE
      Serial.print("Value over serial received: ");
      Serial.print(receivedNumber);
      Serial.print(" Letter: ");
      Serial.print(letters[receivedNumber]);
      Serial.println();
      #endif
    */
    //rotate to new letter
    rotateToLetter(receivedNumber);
  }

#ifdef SERIAL_ENABLE
  // check if we got calibration data via serial
  recvWithEndMarker();
#endif
}

int translateLettertoInt(char letterchar) {
  // automatically convert lower case to upper case
  if ('a' <= letterchar && letterchar <= 'z') {
    letterchar -= 32;
  }
  for (int flapIndex = 0; flapIndex < sizeof(letters); flapIndex++) {
    if (letterchar == letters[flapIndex]) {
      return flapIndex;
    }
  }

  return -1;
}

// Absolute step position of a flap (integer math, rounded). Using absolute
// positions makes every flap always end at the same step count, independent
// of the path taken, and needs no float / accumulated remainder.
static int16_t flapToSteps(uint8_t flap) {
  return (int16_t)(((int32_t)flap * STEPS + AMOUNTFLAPS / 2) / AMOUNTFLAPS);
}

//rotate to letter
void rotateToLetter(int toLetter) {
  if (lastRotation == 0 || (millis() - lastRotation > OVERHEATINGTIMEOUT * 1000)) {
    lastRotation = millis();

    // After a failed homing the position is unknown. Drop requests (so the main
    // loop does not retry endlessly and overheat the stepper) and retry homing
    // only after a longer pause.
    if (calibrationFailed) {
      if (millis() - calibrationFailedAt < CAL_RETRY_TIMEOUT * 1000UL ||
          calibrate(true) < 0) {
        receivedNumber = displayedLetter;
        return;
      }
    }

    //get letter position
    int posLetter = -1;
    posLetter = toLetter;
    int posCurrentLetter = -1;
    posCurrentLetter = displayedLetter;
#ifdef SERIAL_ENABLE
    Serial.print(F("go to letter: "));
    Serial.println(letters[toLetter]);
#endif
    //go to letter, but only if available (>-1)
    if (posLetter > -1) { //check if letter exists
      //check if letter is on higher index, then no full rotaion is needed
      if (posLetter >= posCurrentLetter) {
#ifdef SERIAL_ENABLE
        Serial.println("direct");
#endif
        //go directly to next letter
        startMotor();
        stepper.setSpeed(stepperSpeed);
        stepper.step(ROTATIONDIRECTION * (flapToSteps(posLetter) - flapToSteps(posCurrentLetter)));
      }
      else {
        //full rotation is needed, good time for a calibration
#ifdef SERIAL_ENABLE
        Serial.println(F("full rotation incl. calibration"));
#endif
        if (calibrate(false) < 0) { //calibrate revolver and do not stop motor
          // homing failed: motor is already stopped, abort instead of moving blind
          receivedNumber = displayedLetter;
          return;
        }
        stepper.setSpeed(stepperSpeed);
        stepper.step(ROTATIONDIRECTION * flapToSteps(posLetter));
      }
      //store new position
      displayedLetter = toLetter;
      //rotation is done, stop the motor
      delay(100); //important to stop rotation before shutting of the motor to avoid rotation after switching off current
      stopMotor();
    }
    else {
#ifdef SERIAL_ENABLE
      Serial.println("letter unknown, go to space");
#endif
      desiredLetter = 0;
    }
  }
}

void receiveLetter(int numBytes) {
  // LED packet: [0xFF, brightness, red, green, blue]
  // Existing flap packets: [letter, speed] or
  // [letter, speed, offset_low, offset_high]

  if (numBytes <= 0) return;

  uint8_t data[5] = {0, 0, 0, 0, 0};
  int count = 0;

  while (Wire.available()) {
    uint8_t value = Wire.read();
    if (count < 5) data[count] = value;
    count++;
  }

  if (data[0] == LED_COMMAND) {
    if (count == 5) {
      ledBrightness = data[1];
      ledRed = data[2];
      ledGreen = data[3];
      ledBlue = data[4];
      ledUpdatePending = true;
    }
    return;
  }

  // Legacy split-flap command.
  // Ignore invalid flap indices (would lose the drum position) and clamp speed.
  if (count >= 2 && data[0] < AMOUNTFLAPS) {
    receivedNumber = data[0];
    stepperSpeed = constrain(data[1], MIN_STEPPER_SPEED, MAX_STEPPER_SPEED);
  }

  if (count >= 4) {
    uint16_t newCalOffset =
      (uint16_t)data[2] | ((uint16_t)data[3] << 8);

    // EEPROM write is done in loop(): it takes ms and must not block the ISR.
    if (newCalOffset <= STEPS) {
      pendingCalOffset = newCalOffset;
      offsetWritePending = true;
    }
  }
}

void requestEvent() {
  // 1 = rotating or a move is still pending (e.g. waiting for the overheating
  // timeout), 2 = last homing failed, 0 = idle. Masters checking "== 1" stay compatible.
  uint8_t status = 0;
  if (currentlyrotating || displayedLetter != receivedNumber) {
    status = 1;
  } else if (calibrationFailed) {
    status = 2;
  }
  Wire.write(status); //send unit status to master
  /*
    #ifdef SERIAL_ENABLE
    Serial.print("Status ");
    Serial.print(currentlyrotating);
    Serial.print(" sent to master");
    Serial.println();
    #endif
  */
}

//returns the adress of the unit as int from 0-15
int getaddress() {
  int address = !digitalRead(ADRESSSW4) + (!digitalRead(ADRESSSW3) * 2) + (!digitalRead(ADRESSSW2) * 4) + (!digitalRead(ADRESSSW1) * 8);
  return address;
}

//gets magnet sensor offset from EEPROM in steps
void getOffset() {
  EEPROM.get(eeAddress, calOffset);
  if (calOffset > STEPS) { // blank EEPROM reads 0xFFFF; (calOffset < 0) never triggers on unsigned
    calOffset = 0;
  }
#ifdef SERIAL_ENABLE
  Serial.print(F("CalOffset from EEPROM: "));
  Serial.print(calOffset);
  Serial.println();
#endif
}

//doing a calibration of the revolver using the hall sensor
int calibrate(bool initialCalibration) {
#ifdef SERIAL_ENABLE
  Serial.println(F("calibrate revolver"));
#endif
  currentlyrotating = 1; //set active state to active
  bool reachedMarker = false;
  stepper.setSpeed(stepperSpeed);
  int i = 0;
  while (!reachedMarker) {
    int currentHallValue = digitalRead(HALLPIN);
    if (currentHallValue == 1 && i == 0) { //already in zero position move out a bit and do the calibration {
      //not reached yet
      i = 50;
      stepper.step(ROTATIONDIRECTION * 50); //move 50 steps to get out of scope of hall
    }
    else if (currentHallValue == 1) {
      //not reached yet
      stepper.step(ROTATIONDIRECTION * 1);
    }
    else {
      //reached marker, go to calibrated offset position
      reachedMarker = true;
      stepper.step(ROTATIONDIRECTION * calOffset);
      displayedLetter = 0;
      calibrationFailed = false;
#ifdef SERIAL_ENABLE
      Serial.println(F("revolver calibrated"));
#endif
      //Only stop motor for initial calibration
      if (initialCalibration) {
        stopMotor();
      }
      return i;
    }
    if (i > 3 * STEPS) {
      //seems that there is a problem with the marker or the sensor. turn of the motor to avoid overheating.
      displayedLetter = 0;
      desiredLetter = 0;
      calibrationFailed = true;
      calibrationFailedAt = millis();
      reachedMarker = true;
#ifdef SERIAL_ENABLE
      Serial.println(F("calibration revolver failed"));
#endif
      stopMotor();
      return -1;
    }
    i++;
  }
  return i;
}

//switching off the motor driver
void stopMotor() {
  lastInd1 = digitalRead(STEPPERPIN1);
  lastInd2 = digitalRead(STEPPERPIN2);
  lastInd3 = digitalRead(STEPPERPIN3);
  lastInd4 = digitalRead(STEPPERPIN4);

  digitalWrite(STEPPERPIN1, LOW);
  digitalWrite(STEPPERPIN2, LOW);
  digitalWrite(STEPPERPIN3, LOW);
  digitalWrite(STEPPERPIN4, LOW);
#ifdef SERIAL_ENABLE
  Serial.println(F("Motor Stop"));
#endif
  currentlyrotating = 0; //set active state to not active
  delay(100);
}

void startMotor() {
#ifdef SERIAL_ENABLE
  Serial.println(F("Motor Start"));
#endif
  currentlyrotating = 1; //set active state to active
  digitalWrite(STEPPERPIN1, lastInd1);
  digitalWrite(STEPPERPIN2, lastInd2);
  digitalWrite(STEPPERPIN3, lastInd3);
  digitalWrite(STEPPERPIN4, lastInd4);
}


void writeToEEPROM(uint16_t offsetValue) {
  //One simple call, with the address first and the object second.
  EEPROM.put(eeAddress, offsetValue);

#ifdef SERIAL_ENABLE
  Serial.print(F("Value written to EEPROM: "));
  Serial.print(offsetValue);
  Serial.println();
#endif
}

#ifdef SERIAL_ENABLE
void recvWithEndMarker() {
  static byte ndx = 0;
  char endMarker = '\n';
  char rc;

  while (Serial.available() > 0 && newData == false) {
    rc = Serial.read();

    if (rc != endMarker) {
      receivedChars[ndx] = rc;
      ndx++;
      if (ndx >= numChars) {
        ndx = numChars - 1;
      }
    }
    else {
      receivedChars[ndx] = '\0'; // terminate the string
      ndx = 0;
      newData = true;
    }
  }

  if (newData == true) {
    newData = false;
    if (receivedChars[0] != 0x00 && receivedChars[1] == 0x00) {
      auto new_letter = translateLettertoInt(receivedChars[0]);
      if (new_letter == -1) {
        Serial.println(F("Letter not found in index"));
      } else {
        stepperSpeed = 12;
        receivedNumber = new_letter;
      }
    } else if (strncmp(receivedChars, "test", numChars) == 0) {
      Serial.println(F("Starting calibration test"));
      calibrate(true);
      run_test();
    } else {
      // convert to int
      calOffset = String(receivedChars).toInt();
      // check if the integer conversion failed
      if (calOffset == 0 && receivedChars[0] != '0') {
        Serial.println(F("Invalid Command"));
      } else {
        // save to eeprom
        writeToEEPROM(calOffset);
        calibrate(true);
      }
    }
  }
}
#endif
