/*
  CAN Pub/Sub Complete Example
  
  This example shows a complete CAN publish/subscribe implementation with both
  broker and client functionality that can be selected at compile time.
  
  This is based on the transceiver example provided and demonstrates:
  - Broker: Topic subscription management and message routing
  - Client: Subscribe, publish, and direct messaging
  - 🔄 Client with automatic subscription restoration
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

#include <SuperCANBus.h>

// ===== CONFIGURATION =====
#define IS_BROKER true  // Set to false for client nodes

// ===== GLOBAL OBJECTS =====
#if IS_BROKER
  CANPubSubBroker pubsub(CAN);
#else
  CANPubSubClient pubsub(CAN);
  String SERIAL_NUMBER;  // For persistent ID and subscription restoration
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
  if (!pubsub.begin()) {
    Serial.println("Starting broker failed!");
    while (1);
  }
  
  Serial.println("Broker started successfully");
  
  pubsub.onClientConnect(onClientConnect);
  pubsub.onPublish(onBrokerPublish);
  pubsub.onDirectMessage(onBrokerDirectMessage);
  
  Serial.println("\nBroker ready!");
}

void loopBroker() {
  pubsub.loop();
  
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input == "stats") {
      Serial.println("\n=== Broker Statistics ===");
      Serial.print("Connected clients: ");
      Serial.println(pubsub.getClientCount());
      Serial.print("Active topics: ");
      Serial.println(pubsub.getSubscriptionCount());
      Serial.print("Uptime: ");
      Serial.print(millis() / 1000);
      Serial.println(" seconds\n");
      
    } else if (input.startsWith("pub:")) {
      int colonPos = input.indexOf(':', 4);
      if (colonPos > 0) {
        String topic = input.substring(4, colonPos);
        String message = input.substring(colonPos + 1);
        uint16_t topicHash = CANPubSubBase::hashTopic(topic);
        pubsub.registerTopic(topic);
        pubsub.broadcastMessage(topicHash, message);
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
        uint8_t clientId = (uint8_t)idStr.toInt();
        pubsub.sendDirectMessage(clientId, message);
        Serial.print("Sent to client ");
        Serial.println(clientId, DEC);
      }
      
    } else if (input == "help") {
      printHelp();
    }
  }
}

void onClientConnect(uint8_t clientId) {
  Serial.print(">>> Client ");
  Serial.print(clientId, DEC);
  Serial.println(" connected");
}

void onBrokerPublish(uint16_t topicHash, const String& topic, const String& message) {
  Serial.print(">>> Publish [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
}

void onBrokerDirectMessage(uint8_t senderId, const String& message) {
  Serial.print(">>> Direct from ");
  Serial.print(senderId, DEC);
  Serial.print(": ");
  Serial.println(message);
}

#endif

// ===== CLIENT FUNCTIONS =====

#ifndef IS_BROKER

void setupClient() {
  // Generate unique serial number for persistent ID
  #ifdef ESP32
    uint64_t chipid = ESP.getEfuseMac();
    SERIAL_NUMBER = "CLIENT_" + String((uint32_t)(chipid >> 32), HEX) + 
                                 String((uint32_t)chipid, HEX);
    SERIAL_NUMBER.toUpperCase();
  #else
    SERIAL_NUMBER = "CLIENT_" + String(random(1000, 9999));
  #endif
  
  Serial.print("Serial Number: ");
  Serial.println(SERIAL_NUMBER);
  Serial.println();
  
  pubsub.onConnect(onClientConnect);
  pubsub.onMessage(onClientMessage);
  pubsub.onDirectMessage(onClientDirectMessage);
  pubsub.onPong(onClientPong);
  
  Serial.println("Connecting to broker...");
  delay(random(100, 500)); // Random delay to avoid collision
  
  // Connect with serial number for persistent ID and subscription restoration
  if (pubsub.begin(SERIAL_NUMBER, 5000)) {
    Serial.print("Connected! Client ID: ");
    Serial.println(pubsub.getClientId(), DEC);
    
    // Check if subscriptions were restored
    uint8_t subCount = pubsub.getSubscriptionCount();
    if (subCount > 0) {
      Serial.println();
      Serial.println("🔄 Subscriptions automatically restored!");
      Serial.print("   Restored ");
      Serial.print(subCount);
      Serial.print(" subscription(s): ");
      
      bool first = true;
      pubsub.listSubscribedTopics([&first](uint16_t hash, const String& name) {
        if (!first) Serial.print(", ");
        Serial.print(name);
        first = false;
      });
      Serial.println();
      Serial.println("   ℹ️  No need to re-subscribe!");
    }
  } else {
    Serial.println("Failed to connect to broker!");
  }
}

void loopClient() {
  pubsub.loop();
  
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (!pubsub.isConnected()) {
      Serial.println("Not connected to broker!");
      return;
    }
    
    if (input.startsWith("sub:")) {
      String topic = input.substring(4);
      if (pubsub.subscribe(topic)) {
        Serial.print("Subscribed to: ");
        Serial.println(topic);
      }
      
    } else if (input.startsWith("unsub:")) {
      String topic = input.substring(6);
      if (pubsub.unsubscribe(topic)) {
        Serial.print("Unsubscribed from: ");
        Serial.println(topic);
      }
      
    } else if (input.startsWith("pub:")) {
      int colonPos = input.indexOf(':', 4);
      if (colonPos > 0) {
        String topic = input.substring(4, colonPos);
        String message = input.substring(colonPos + 1);
        if (pubsub.publish(topic, message)) {
          Serial.print("Published to [");
          Serial.print(topic);
          Serial.print("]: ");
          Serial.println(message);
        }
      }
      
    } else if (input.startsWith("msg:")) {
      String message = input.substring(4);
      if (pubsub.sendDirectMessage(message)) {
        Serial.print("Sent to broker: ");
        Serial.println(message);
      }
      
    } else if (input == "ping") {
      if (pubsub.ping()) {
        Serial.println("Ping sent");
      }
      
    } else if (input == "status") {
      Serial.println("\n=== Status ===");
      Serial.print("Connected: Yes, ID: ");
      Serial.println(pubsub.getClientId(), DEC);
      Serial.print("Serial Number: ");
      Serial.println(SERIAL_NUMBER);
      Serial.print("Subscriptions: ");
      Serial.println(pubsub.getSubscriptionCount());
      
      if (pubsub.getSubscriptionCount() > 0) {
        Serial.print("Topics: ");
        bool first = true;
        pubsub.listSubscribedTopics([&first](uint16_t hash, const String& name) {
          if (!first) Serial.print(", ");
          Serial.print(name);
          first = false;
        });
        Serial.println();
      }
      
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
  Serial.print(">>> Direct from ");
  Serial.print(senderId, DEC);
  Serial.print(": ");
  Serial.println(message);
}

void onClientPong() {
  unsigned long rtt = pubsub.getLastPingTime();
  Serial.print(">>> Pong from broker [");
  Serial.print(rtt);
  Serial.println("ms]");
}

#endif

// ===== COMMON FUNCTIONS =====

void printHelp() {
  Serial.println("\n=== Commands ===");
  
#if IS_BROKER
  Serial.println("  stats              - Show broker statistics");
  Serial.println("  pub:topic:message  - Publish to topic");
  Serial.println("  msg:id:message     - Send to client (id in decimal)");
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
