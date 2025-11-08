/*
  CAN Pub/Sub Complete Example
  
  This example shows a complete CAN publish/subscribe implementation with both
  broker and client functionality that can be selected at compile time.
  
  This is based on the transceiver example provided and demonstrates:
  - Broker: Topic subscription management and message routing
  - Client: Subscribe, publish, and direct messaging
  - Interactive serial interface
  
  Circuit:
  - MCP2515 CAN module connected to SPI pins
  - Or ESP32 with built-in CAN controller
  
  How to use:
  1. Set IS_BROKER to true for broker node
  2. Set IS_BROKER to false for client nodes
  3. Upload to multiple devices
  4. Use serial monitor to interact with each node
  
  Created 2025
  by Juan Pablo Risso
  Based on the CAN publish/subscribe protocol
*/

#include <SUPER_CAN.h>

// ===== CONFIGURATION =====
#define IS_BROKER true  // Set to false for client nodes

// ===== GLOBAL OBJECTS =====
#if IS_BROKER
  CANMqttBroker mqtt(CAN);
#else
  CANMqttClient mqtt(CAN);
#endif

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println(IS_BROKER ? "\n=== CAN Pub/Sub Broker ===" : "\n=== CAN Pub/Sub Client ===");
  Serial.println("Initializing CAN bus...");

  // Initialize CAN bus at 500 kbps
  if (!CAN.begin(500E3)) {
    Serial.println("Starting CAN failed!");
    while (1);
  }
  
  Serial.println("CAN bus initialized");
  
#if IS_BROKER
  setupBroker();
#else
  setupClient();
#endif
  
  printHelp();
}

void loop() {
#if IS_BROKER
  loopBroker();
#else
  loopClient();
#endif
}

// ===== BROKER FUNCTIONS =====

#if IS_BROKER

void setupBroker() {
  if (!mqtt.begin()) {
    Serial.println("Starting broker failed!");
    while (1);
  }
  
  Serial.println("Broker started successfully");
  
  mqtt.onClientConnect(onClientConnect);
  mqtt.onPublish(onBrokerPublish);
  mqtt.onDirectMessage(onBrokerDirectMessage);
  
  Serial.println("\nBroker ready!");
}

void loopBroker() {
  mqtt.loop();
  
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input == "stats") {
      Serial.println("\n=== Broker Statistics ===");
      Serial.print("Connected clients: ");
      Serial.println(mqtt.getClientCount());
      Serial.print("Active topics: ");
      Serial.println(mqtt.getSubscriptionCount());
      Serial.print("Uptime: ");
      Serial.print(millis() / 1000);
      Serial.println(" seconds\n");
      
    } else if (input.startsWith("pub:")) {
      int colonPos = input.indexOf(':', 4);
      if (colonPos > 0) {
        String topic = input.substring(4, colonPos);
        String message = input.substring(colonPos + 1);
        uint16_t topicHash = CANMqttBase::hashTopic(topic);
        mqtt.registerTopic(topic);
        mqtt.broadcastMessage(topicHash, message);
        Serial.print("Published to [");
        Serial.print(topic);
        Serial.print("]: ");
        Serial.println(message);
      }
      
    } else if (input.startsWith("msg:")) {
      int colonPos = input.indexOf(':', 4);
      if (colonPos > 0) {
        String idStr = input.substring(4, colonPos);
        String message = input.substring(colonPos + 1);
        uint8_t clientId = (uint8_t)strtol(idStr.c_str(), NULL, 16);
        mqtt.sendDirectMessage(clientId, message);
        Serial.print("Sent to client 0x");
        Serial.println(clientId, HEX);
      }
      
    } else if (input == "help") {
      printHelp();
    }
  }
}

void onClientConnect(uint8_t clientId) {
  Serial.print(">>> Client 0x");
  Serial.print(clientId, HEX);
  Serial.println(" connected");
}

void onBrokerPublish(uint16_t topicHash, const String& topic, const String& message) {
  Serial.print(">>> Publish [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
}

void onBrokerDirectMessage(uint8_t senderId, const String& message) {
  Serial.print(">>> Direct from 0x");
  Serial.print(senderId, HEX);
  Serial.print(": ");
  Serial.println(message);
}

#endif

// ===== CLIENT FUNCTIONS =====

#ifndef IS_BROKER

void setupClient() {
  mqtt.onConnect(onClientConnect);
  mqtt.onMessage(onClientMessage);
  mqtt.onDirectMessage(onClientDirectMessage);
  
  Serial.println("Connecting to broker...");
  delay(random(100, 500)); // Random delay to avoid collision
  
  if (mqtt.begin(5000)) {
    Serial.print("Connected! Client ID: 0x");
    Serial.println(mqtt.getClientId(), HEX);
  } else {
    Serial.println("Failed to connect to broker!");
  }
}

void loopClient() {
  mqtt.loop();
  
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (!mqtt.isConnected()) {
      Serial.println("Not connected to broker!");
      return;
    }
    
    if (input.startsWith("sub:")) {
      String topic = input.substring(4);
      if (mqtt.subscribe(topic)) {
        Serial.print("Subscribed to: ");
        Serial.println(topic);
      }
      
    } else if (input.startsWith("unsub:")) {
      String topic = input.substring(6);
      if (mqtt.unsubscribe(topic)) {
        Serial.print("Unsubscribed from: ");
        Serial.println(topic);
      }
      
    } else if (input.startsWith("pub:")) {
      int colonPos = input.indexOf(':', 4);
      if (colonPos > 0) {
        String topic = input.substring(4, colonPos);
        String message = input.substring(colonPos + 1);
        if (mqtt.publish(topic, message)) {
          Serial.print("Published to [");
          Serial.print(topic);
          Serial.print("]: ");
          Serial.println(message);
        }
      }
      
    } else if (input.startsWith("msg:")) {
      String message = input.substring(4);
      if (mqtt.sendDirectMessage(message)) {
        Serial.print("Sent to broker: ");
        Serial.println(message);
      }
      
    } else if (input == "ping") {
      if (mqtt.ping()) {
        Serial.println("Ping sent");
      }
      
    } else if (input == "status") {
      Serial.println("\n=== Status ===");
      Serial.print("Connected: Yes, ID: 0x");
      Serial.println(mqtt.getClientId(), HEX);
      Serial.print("Subscriptions: ");
      Serial.println(mqtt.getSubscriptionCount());
      Serial.print("Uptime: ");
      Serial.print(millis() / 1000);
      Serial.println(" seconds\n");
      
    } else if (input == "help") {
      printHelp();
    }
  }
}

void onClientConnect() {
  Serial.println(">>> Connected to broker!");
}

void onClientMessage(uint16_t topicHash, const String& topic, const String& message) {
  Serial.print(">>> [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
}

void onClientDirectMessage(uint8_t senderId, const String& message) {
  Serial.print(">>> Direct from 0x");
  Serial.print(senderId, HEX);
  Serial.print(": ");
  Serial.println(message);
}

#endif

// ===== COMMON FUNCTIONS =====

void printHelp() {
  Serial.println("\n=== Commands ===");
  
#if IS_BROKER
  Serial.println("  stats              - Show broker statistics");
  Serial.println("  pub:topic:message  - Publish to topic");
  Serial.println("  msg:id:message     - Send to client (id in hex)");
#else
  Serial.println("  sub:topic          - Subscribe to topic");
  Serial.println("  unsub:topic        - Unsubscribe from topic");
  Serial.println("  pub:topic:message  - Publish to topic");
  Serial.println("  msg:message        - Send direct message to broker");
  Serial.println("  ping               - Ping broker");
  Serial.println("  status             - Show connection status");
#endif
  
  Serial.println("  help               - Show this help");
  Serial.println();
}
