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

WiFiClient espClient;
PubSubClient client(espClient);

const uint8_t MSG_QUEUE_SIZE = 10;
QueueEntry messageQueue[MSG_QUEUE_SIZE];
uint8_t queueHead = 0;
uint8_t queueTail = 0;
uint8_t queueCount = 0;

// PubSubClient packet buffer (topic + payload + header)
const uint16_t MQTT_BUFFER_SIZE = 512;

// Current animation state
AnimationType currentAnimation = ANIM_INSTANT;
uint8_t currentSpeed = 10;

// Health metrics
uint32_t messagesProcessed = 0;
uint32_t mqttDisconnects = 0;

void mqttSetup() {
  // Use config values from WiFiManager
  int port = atoi(cfg_mqtt_port);
  if (port == 0) port = 1883;

  Serial.print("Setting up MQTT: ");
  Serial.print(cfg_mqtt_server);
  Serial.print(":");
  Serial.println(port);

  client.setServer(cfg_mqtt_server, port);
  client.setCallback(callback);

  // PubSubClient default is 256 bytes for topic + payload. Larger publishes
  // (registration JSON) and incoming messages are otherwise dropped silently.
  if (!client.setBufferSize(MQTT_BUFFER_SIZE)) {
    Serial.println("Failed to allocate MQTT buffer");
  }
}

// Non-blocking reconnect - returns true if connected, false if still trying
bool reconnect() {
  static unsigned long lastReconnectAttempt = 0;

  if (client.connected()) {
    return true;
  }

  unsigned long now = millis();
  // Only attempt reconnect every 5 seconds
  if (now - lastReconnectAttempt < 5000) {
    return false;
  }
  lastReconnectAttempt = now;
  mqttDisconnects++;

  Serial.print("Attempting MQTT connection...");

  // Connect with LWT (Last Will and Testament) for offline detection.
  // The client ID is the MAC-based device_id: it is unique per device, whereas
  // the configurable device name defaults to "flaps-01" on every unit and
  // identical IDs make the broker kick the other client in a loop.
  bool connected = false;
  if (strlen(cfg_mqtt_user) > 0) {
    connected = client.connect(device_id, cfg_mqtt_user, cfg_mqtt_pass,
                               mqtt_status_topic, 0, true, "offline");
  } else {
    connected = client.connect(device_id, NULL, NULL,
                               mqtt_status_topic, 0, true, "offline");
  }

  if (connected) {
    Serial.println("connected");

    // Subscribe to all command topics
    client.subscribe(mqtt_command);     // Device-specific: /flaps/{device}/command
    client.subscribe(mqtt_broadcast);   // Fleet-wide: /flaps/all/command
    client.subscribe(mqtt_listen);      // Legacy: /game

    // Publish online status
    client.publish(mqtt_status_topic, "online", true);

    Serial.print("Subscribed to: ");
    Serial.println(mqtt_command);
    return true;
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    return false;
  }
}

// Animation names accepted in the MQTT "animation" field (case-insensitive)
struct AnimationName {
  const char* name;
  AnimationType type;
};

static const AnimationName ANIMATION_NAMES[] = {
  {"instant",      ANIM_INSTANT},
  {"wave",         ANIM_WAVE},
  {"scroll",       ANIM_SCROLL},
  {"random",       ANIM_RANDOM},
  {"slot_machine", ANIM_SLOT_MACHINE},
  {"slot",         ANIM_SLOT_MACHINE},
  {"scramble",     ANIM_SCRAMBLE},
  {"center_out",   ANIM_CENTER_OUT},
  {"center",       ANIM_CENTER_OUT},
  {"typewriter",   ANIM_TYPEWRITER},
  {"matrix",       ANIM_MATRIX},
  {"countdown",    ANIM_COUNTDOWN},
  {"cascade",      ANIM_CASCADE},
  {"spin",         ANIM_SPIN},
  {"glitch",       ANIM_GLITCH},
  {"wipe",         ANIM_WIPE}
};

// Parse animation type from string (unknown names fall back to instant)
AnimationType parseAnimation(const char* anim) {
  if (anim != nullptr) {
    for (const AnimationName& entry : ANIMATION_NAMES) {
      if (strcasecmp(anim, entry.name) == 0) {
        return entry.type;
      }
    }
    Serial.print("Unknown animation '");
    Serial.print(anim);
    Serial.println("', using instant");
  }
  return ANIM_INSTANT;
}

// Add message to queue with animation
bool enqueueMessage(const char* msg, AnimationType anim, uint8_t speed, uint32_t delayMs) {
  if (queueCount >= MSG_QUEUE_SIZE) {
    Serial.println("Queue full, dropping message");
    return false;
  }
  strncpy(messageQueue[queueTail].text, msg, number_units);
  messageQueue[queueTail].text[number_units] = '\0';
  messageQueue[queueTail].animation = anim;
  messageQueue[queueTail].speed = speed;
  messageQueue[queueTail].delayMs = delayMs;
  queueTail = (queueTail + 1) % MSG_QUEUE_SIZE;
  queueCount++;
  return true;
}

// Center-pad one chunk and put it into the queue
static bool enqueueChunk(const char* chunk, size_t len,
                         AnimationType anim, uint8_t speed, uint32_t delayMs) {
  char displayText[number_units + 1];
  centerPadMessage(chunk, (int)len, displayText);
  return enqueueMessage(displayText, anim, speed, delayMs);
}

// Split text at word boundaries and enqueue display-sized chunks.
// Chunks go straight into the queue (no large temporary arrays on the small
// ESP8266 stack). Words longer than the display are split across chunks.
// If the queue runs full, the queue is rolled back so the text is dropped
// completely instead of being shown partially.
bool enqueueTextChunks(const char* text,
                       AnimationType anim,
                       uint8_t speed,
                       uint32_t delayMs) {

  const uint8_t savedTail = queueTail;
  const uint8_t savedCount = queueCount;

  char current[number_units + 1];
  size_t currentLength = 0;
  uint8_t chunkCount = 0;
  bool ok = true;

  const char* p = text;

  while (ok && *p) {

    // Skip leading spaces
    while (*p == ' ') {
      p++;
    }
    if (*p == '\0') {
      break;
    }

    // Find end of word
    const char* wordEnd = p;
    while (*wordEnd != '\0' && *wordEnd != ' ') {
      wordEnd++;
    }
    size_t wordLength = wordEnd - p;

    // Add the word piece by piece (pieces are at most one display wide)
    while (ok && wordLength > 0) {
      size_t piece = (wordLength > number_units) ? number_units : wordLength;

      // Piece does not fit into the current chunk: flush it first
      if (currentLength > 0 && currentLength + 1 + piece > number_units) {
        ok = enqueueChunk(current, currentLength, anim, speed, delayMs);
        if (ok) {
          chunkCount++;
        }
        currentLength = 0;
        continue;
      }

      if (currentLength > 0) {
        current[currentLength++] = ' ';
      }
      memcpy(current + currentLength, p, piece);
      currentLength += piece;

      p += piece;
      wordLength -= piece;
    }
  }

  // Store last chunk
  if (ok && currentLength > 0) {
    ok = enqueueChunk(current, currentLength, anim, speed, delayMs);
    if (ok) {
      chunkCount++;
    }
  }

  if (!ok) {
    queueTail = savedTail;
    queueCount = savedCount;
    Serial.println("Queue full - dropping complete text");
    return false;
  }

  messagesProcessed += chunkCount;

  Serial.print("Text split into ");
  Serial.print(chunkCount);
  Serial.println(" word-based chunks");

  return true;
}

// Get next message from queue
bool dequeueMessage(QueueEntry* entry) {
  if (queueCount == 0) {
    return false;
  }
  *entry = messageQueue[queueHead];
  queueHead = (queueHead + 1) % MSG_QUEUE_SIZE;
  queueCount--;
  return true;
}

// Center-pad a message
void centerPadMessage(const char* input, int inputLen, char* output) {
  int spaces = 0;
  if (inputLen < number_units) {
    spaces = (number_units - inputLen) / 2;
  }
  for (int i = 0; i < number_units + 1; i++) {
    if (i < spaces) {
      output[i] = ' ';
    } else if (i - spaces < inputLen) {
      output[i] = input[i - spaces];
    } else if (i == number_units) {
      output[i] = '\0';
    } else {
      output[i] = ' ';
    }
  }
}

void callback(String topic, byte* message, unsigned int length) {
  char flap_msg[number_units + 1];
  AnimationType anim = ANIM_INSTANT;
  uint8_t speed = flap_speed;

  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Raw: ");

  // Null-terminate message for parsing
  char msgBuffer[256];
  int msgLen = (length < 255) ? length : 255;
  memcpy(msgBuffer, message, msgLen);
  msgBuffer[msgLen] = '\0';
  Serial.println(msgBuffer);

  uint32_t delayMs = 1000;

  // Check if message is JSON (starts with '{')
  if (msgBuffer[0] == '{') {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, msgBuffer);

    if (!error) {
      // Parse JSON payload: {"text":"HELLO","animation":"wave","speed":5}

      // Check for special commands
      const char* command = doc["command"] | "";

      // Check for LED commands
      if (strcmp(command, "led") == 0 ||
          strcmp(command, "led_off") == 0 ||
          strcmp(command, "led_all") == 0 ||
          strcmp(command, "led_off_all") == 0) {

        bool allUnits =
          (strcmp(command, "led_all") == 0 ||
           strcmp(command, "led_off_all") == 0);

        bool ledOff =
          (strcmp(command, "led_off") == 0 ||
           strcmp(command, "led_off_all") == 0);

        uint8_t brightness = 0;
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;

        // Read color and brightness if LED should be on
        if (!ledOff) {

          brightness = constrain(
            (int)(doc["brightness"] | 255), 0, 255);

          red = constrain(
            (int)(doc["red"] | 0), 0, 255);

          green = constrain(
            (int)(doc["green"] | 0), 0, 255);

          blue = constrain(
            (int)(doc["blue"] | 0), 0, 255);
        }

        if (allUnits) {

          Serial.println("Setting LEDs on all units");

          // Set LED on every Split-Flap unit
          for (uint8_t i = 1; i <= number_units; i++) {

            writeLedToUnit(
              i,
              brightness,
              red,
              green,
              blue
            );

            Serial.print("LED updated - Unit: ");
            Serial.println(i);
          }

        } else {

          // Set LED on a single unit
          int address = doc["address"] | -1;

          if (address < 1 || address > number_units) {
            Serial.println("Invalid I2C address");
            return;
          }

          writeLedToUnit(
            (uint8_t)address,
            brightness,
            red,
            green,
            blue
          );

          Serial.print("LED updated - Unit: ");
          Serial.println(address);
        }

        return;
      }

      if (strcmp(command, "clear") == 0) {

        Serial.println("Clear command received");

        // Get animation parameters
        const char* animStr = doc["animation"] | "instant";

        speed = doc["speed"] | flap_speed;
        anim = parseAnimation(animStr);

        // Fill display with spaces
        memset(flap_msg, ' ', number_units);
        flap_msg[number_units] = '\0';

        // Add clear command to message queue
        if (enqueueMessage(flap_msg, anim, speed, 0)) {
          messagesProcessed++;
          Serial.print("Display clear queued - animation: ");
          Serial.print(animStr);
          Serial.print(", speed: ");
          Serial.println(speed);
        }

        return;
      }

      const char* text = doc["text"] | "";
      const char* animStr = doc["animation"] | "instant";
      speed = doc["speed"] | flap_speed;
      delayMs = doc["delay"] | 1000UL;

      // Limit to 60s
      if (delayMs > 60000) {
        delayMs = 60000;
      }

      anim = parseAnimation(animStr);

      Serial.print("JSON parsed - animation: ");
      Serial.print(animStr);
      Serial.print(", speed: ");
      Serial.println(speed);

      // Split text into chunks and enqueue (text points into doc, still valid)
      enqueueTextChunks(text, anim, speed, delayMs);
      return;
    }

    Serial.println("JSON parse failed, treating as plain text");
  }

  // Plain text payload (also the legacy /game topic): strip trailing
  // whitespace/newlines, then show it with the default animation.
  while (msgLen > 0 && isspace((unsigned char)msgBuffer[msgLen - 1])) {
    msgBuffer[--msgLen] = '\0';
  }
  Serial.println("Plain text message");
  enqueueTextChunks(msgBuffer, anim, speed, delayMs);
}

void mqttLoopOps() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}

// Process queued messages (call from main loop)
void processMessageQueue() {
  static QueueEntry nextEntry;

  static bool messageInProgress = false;
  static unsigned long nextMessageAllowedAt = 0;
  static uint32_t currentDelayMs = 0;

  
  if (messageInProgress) {
    if (!isDisplayMoving()) {
      messageInProgress = false;
      nextMessageAllowedAt = millis() + currentDelayMs;
    }
    return;
  }

  if (queueCount == 0) {
    return;
  }

  if ((long)(millis() - nextMessageAllowedAt) < 0) {
    return;
  }

  if (dequeueMessage(&nextEntry)) {
    currentAnimation = nextEntry.animation;
    currentSpeed = nextEntry.speed;
    currentDelayMs = nextEntry.delayMs;

    showNewDataAnimated(
      nextEntry.text,
      nextEntry.animation,
      nextEntry.speed
    );

    messageInProgress = true;
  }
}

void build_registration() {
  JsonDocument doc;
  doc["name"] = cfg_device_name;
  doc["device_id"] = device_id;
  doc["ip_address"] = ip_char;
  doc["state"] = "connected";
  doc["uptime"] = millis();
  doc["fw_version"] = FW_VERSION;
  serializeJson(doc, reg);
}

void checkIn() {
  unsigned long uptime = millis();
  char registration[MQTT_BUFFER_SIZE];
  if (uptime - lastMillis > check_in_freq) {
    JsonDocument doc;
    deserializeJson(doc, reg);
    doc["uptime"] = uptime;
    doc["last_written"] = writtenLast;
    doc["queue_depth"] = queueCount;
    serializeJson(doc, registration);
    lastMillis = uptime;
    if (!client.publish(mqtt_registration_topic, registration)) {
      Serial.println("Registration publish failed (not connected or too large)");
    }
  }
}

// Publish health metrics
void publishHealth() {
  static unsigned long lastHealthPublish = 0;
  unsigned long now = millis();

  // Publish every 30 seconds
  if (now - lastHealthPublish < 30000) {
    return;
  }
  lastHealthPublish = now;

  JsonDocument doc;
  doc["uptime"] = now;
  doc["messages_processed"] = messagesProcessed;
  doc["mqtt_disconnects"] = mqttDisconnects;
  doc["i2c_errors"] = getTotalI2CErrors();
  doc["queue_depth"] = queueCount;
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();

  char healthJson[512];
  serializeJson(doc, healthJson);
  client.publish(mqtt_health, healthJson);
}
