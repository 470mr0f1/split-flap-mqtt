#include "declarations.h"

const uint8_t MSG_QUEUE_SIZE = 10;
QueueEntry messageQueue[MSG_QUEUE_SIZE];
uint8_t queueHead = 0;
uint8_t queueTail = 0;
uint8_t queueCount = 0;

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

  // Connect with LWT (Last Will and Testament) for offline detection
  // Use config values from WiFiManager
  bool connected = false;
  if (strlen(cfg_mqtt_user) > 0) {
    connected = client.connect(cfg_device_name, cfg_mqtt_user, cfg_mqtt_pass,
                               mqtt_status_topic, 0, true, "offline");
  } else {
    connected = client.connect(cfg_device_name, NULL, NULL,
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

// Parse animation type from string
AnimationType parseAnimation(const char* anim) {
  if (strcmp(anim, "wave") == 0) return ANIM_WAVE;
  if (strcmp(anim, "scroll") == 0) return ANIM_SCROLL;
  if (strcmp(anim, "random") == 0) return ANIM_RANDOM;
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

// Split text at word boundaries and enqueue display-sized chunks
bool enqueueTextChunks(const char* text,
                       AnimationType anim,
                       uint8_t speed,
                       uint32_t delayMs) {

    const uint8_t chunkSize = 10;

    char chunks[26][256];
    uint8_t chunkCount = 0;

    char currentChunk[256] = "";
    size_t currentLength = 0;

    const char* word = text;

    while (*word) {

        // Skip leading spaces
        while (*word == ' ') {
            word++;
        }

        if (*word == '\0') {
            break;
        }

        // Find next word
        const char* wordEnd = word;

        while (*wordEnd != '\0' && *wordEnd != ' ') {
            wordEnd++;
        }

        size_t wordLength = wordEnd - word;

        // Calculate required space
        size_t requiredLength = currentLength == 0
                              ? wordLength
                              : currentLength + 1 + wordLength;

        // Start new chunk if word does not fit
        if (currentLength > 0 && requiredLength > chunkSize) {

            if (chunkCount >= 26) {
                Serial.println("Too many text chunks");
                return false;
            }

            strcpy(chunks[chunkCount++], currentChunk);

            currentChunk[0] = '\0';
            currentLength = 0;
        }

        // Add word to current chunk
        if (currentLength > 0) {
            currentChunk[currentLength++] = ' ';
        }

        memcpy(currentChunk + currentLength, word, wordLength);

        currentLength += wordLength;
        currentChunk[currentLength] = '\0';

        word = wordEnd;
    }

    // Store last chunk
    if (currentLength > 0) {

        if (chunkCount >= 26) {
            Serial.println("Too many text chunks");
            return false;
        }

        strcpy(chunks[chunkCount++], currentChunk);
    }

    // Check queue capacity before adding anything
    if (chunkCount > MSG_QUEUE_SIZE - queueCount) {
        Serial.println("Queue full - dropping complete text");
        return false;
    }

    // Enqueue all chunks
    for (uint8_t i = 0; i < chunkCount; i++) {

        char displayText[number_units + 1];

        centerPadMessage(
            chunks[i],
            strlen(chunks[i]),
            displayText
        );

        if (!enqueueMessage(displayText, anim, speed, delayMs)) {
            return false;
        }

        messagesProcessed++;
    }

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
  char textToQueue[256];
  int msgLen = (length < 255) ? length : 255;
  memcpy(msgBuffer, message, msgLen);
  msgBuffer[msgLen] = '\0';
  Serial.println(msgBuffer);

  strncpy(textToQueue, msgBuffer, sizeof(textToQueue) - 1);
  textToQueue[sizeof(textToQueue) - 1] = '\0';

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

      msgLen = strlen(text);
      
      strncpy(textToQueue, text, sizeof(textToQueue) - 1);
      textToQueue[sizeof(textToQueue) - 1] = '\0';

      Serial.print("JSON parsed - animation: ");
      Serial.print(animStr);
      Serial.print(", speed: ");
      Serial.println(speed);

      // Split text into chunks and enqueue
      enqueueTextChunks(textToQueue, anim, speed, delayMs);
  

    } 
    
    else {
      // JSON parse failed, treat as plain text
      Serial.println("JSON parse failed, treating as plain text");
      } 
  }

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
  char registration[1024];
  if (uptime - lastMillis > check_in_freq) {
    JsonDocument doc;
    deserializeJson(doc, reg);
    doc["uptime"] = uptime;
    doc["last_written"] = writtenLast;
    doc["queue_depth"] = queueCount;
    serializeJson(doc, registration);
    lastMillis = uptime;
    client.publish(mqtt_registration_topic, registration);
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
