#ifndef DECLARATIONS_H
#define DECLARATIONS_H

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


#pragma once

// Version
extern const char* FW_VERSION;
extern const int check_in_freq;

// Constants
constexpr int number_units = 10;
extern const int flapamount;
extern const int flap_speed;
extern const char letters[];
extern char writtenLast[];
extern int displayState[];
extern const uint8_t answersize;

// WiFiManager config variables (defined in 02_wifi.ino)
extern char cfg_mqtt_server[64];
extern char cfg_mqtt_port[6];
extern char cfg_mqtt_user[32];
extern char cfg_mqtt_pass[32];
extern char cfg_device_name[32];

// Global variables for network (defined in 01_config.ino)
extern IPAddress ip_address;
extern char ip_char[16];
extern char device_id[18];
extern char mqtt_command[64];
extern char mqtt_status_topic[64];
extern char mqtt_health[64];
extern char mqtt_registration_topic[64];
extern const char* mqtt_broadcast;
extern const char* mqtt_listen;
extern unsigned long lastMillis;
extern String reg;

// Animation types
enum AnimationType {
  ANIM_INSTANT = 0,
  ANIM_WAVE,
  ANIM_SCROLL,
  ANIM_RANDOM,
  ANIM_SLOT_MACHINE,
  ANIM_SCRAMBLE,
  ANIM_CENTER_OUT,
  ANIM_TYPEWRITER,
  ANIM_MATRIX,
  ANIM_COUNTDOWN,
  ANIM_CASCADE,
  ANIM_SPIN,
  ANIM_GLITCH,
  ANIM_WIPE
};

// Queue entry structure
struct QueueEntry {
  char text[11];  // number_units + 1
  AnimationType animation;
  uint8_t speed;
  uint32_t delayMs;
};

// Function declarations - Config
void buildDeviceId();
void buildMqttTopics();

// Function declarations - WiFi
void connectToWifi();
void checkWifiConnection();
void resetWifiConfig();
bool saveConfig();

// Function declarations - OTA
void otaSetup();

// Function declarations - Web Config
void webConfigSetup();
void webConfigLoop();

// Function declarations - MQTT
void mqttSetup();
bool reconnect();
void callback(String topic, byte* message, unsigned int length);
void mqttLoopOps();
void processMessageQueue();
void build_registration();
void checkIn();
void publishHealth();
bool enqueueTextChunks(const char* text, AnimationType anim, uint8_t speed, uint32_t delayMs);
bool enqueueMessage(const char* msg, AnimationType anim, uint8_t speed, uint32_t delayMs);
bool dequeueMessage(QueueEntry* entry);

// Function declarations - Flaps
void showNewData(char message[]);
void showNewDataAnimated(char message[], AnimationType anim, uint8_t speed);
void showMessage(char message[], int flapSpeed);
void showMessageAnimated(char message[number_units+1],int flapSpeed, AnimationType anim);
void waitForDisplayIdle();
void showWaveAnimation(char message[], int flapSpeed);
void showScrollAnimation(char message[], int flapSpeed);
void showRandomAnimation(char message[], int flapSpeed);
void initializeAnimationRandom();
uint8_t getRandomFlapPosition(uint8_t excludedPosition);
uint8_t getMessagePosition(char character);
void showSlotMachineAnimation(char message[number_units+1], int flapSpeed);
void showScrambleAnimation(char message[number_units+1], int flapSpeed);
void showCenterOutAnimation(char message[number_units+1], int flapSpeed);
void showTypewriterAnimation(char message[number_units+1], int flapSpeed);
void showMatrixAnimation(char message[number_units+1], int flapSpeed);
void showCountdownAnimation(char message[number_units+1], int flapSpeed);
void showCascadeAnimation(char message[number_units+1], int flapSpeed);
void showSpinAnimation(char message[number_units+1], int flapSpeed);
void showGlitchAnimation(char message[number_units+1], int flapSpeed);
void showWipeAnimation(char message[number_units+1], int flapSpeed);

uint8_t translateLettertoInt(char letterchar);
void writeToUnit(uint8_t address, uint8_t letter, int flapSpeed);
void writeLedToUnit(uint8_t address, uint8_t brightness, uint8_t red, uint8_t green, uint8_t blue);
bool isDisplayMoving();
int checkIfMoving(uint8_t address);
uint32_t getTotalI2CErrors();

#endif
