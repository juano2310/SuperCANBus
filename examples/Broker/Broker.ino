/*
  CAN Pub/Sub Broker Example
  
  This example demonstrates how to create a CAN bus publish/subscribe broker
  that manages topic subscriptions and message forwarding between clients.
  
  Features:
  - Automatic client ID assignment
  - Topic subscription management
  - Message routing to subscribers
  - Direct messaging support
  - Serial command interface
  
  Circuit:
  - MCP2515 CAN module connected to SPI pins
  - Or ESP32 with built-in CAN controller
  
  Commands (via Serial):
  - list          - List all connected clients and subscriptions
  - pub:topic:msg - Publish message to a topic
  - msg:id:msg    - Send direct message to client
  - stats         - Show broker statistics
  
  Created 2025
  by Juan Pablo Risso
*/

#include <SUPER_CAN.h>

// Create broker instance
CANPubSubBroker broker(CAN);

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("=== CAN Pub/Sub Broker ===");
  Serial.println("Initializing CAN bus...");

  // Initialize CAN bus at 500 kbps
  if (!CAN.begin(500E3)) {
    Serial.println("Starting CAN failed!");
    while (1);
  }
  
  Serial.println("CAN bus initialized");
  
  // Initialize broker
  if (!broker.begin()) {
    Serial.println("Starting broker failed!");
    while (1);
  }
  
  Serial.println("Broker started successfully");
  
  // Setup callbacks
  broker.onClientConnect(onClientConnect);
  broker.onPublish(onPublish);
  broker.onDirectMessage(onDirectMessage);
  
  Serial.println("\nBroker ready!");
  Serial.println("\nCommands:");
  Serial.println("  list           - List clients and subscriptions");
  Serial.println("  topics         - List subscribed topics");
  Serial.println("  pub:topic:msg  - Publish to topic");
  Serial.println("  msg:id:msg     - Send direct message to client");
  Serial.println("  stats          - Show statistics");
  Serial.println();
}

void loop() {
  // Process incoming CAN messages
  broker.loop();
  
  // Handle serial commands
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input == "list") {
      listClientsAndSubscriptions();
    } else if (input == "topics") {
      listTopics();
    } else if (input == "stats") {
      showStats();
    } else if (input.startsWith("pub:")) {
      int colonPos = input.indexOf(':', 4);
      if (colonPos > 0) {
        String topic = input.substring(4, colonPos);
        String message = input.substring(colonPos + 1);
        publishMessage(topic, message);
      } else {
        Serial.println("Usage: pub:topic:message");
      }
    } else if (input.startsWith("msg:")) {
      int colonPos = input.indexOf(':', 4);
      if (colonPos > 0) {
        String idStr = input.substring(4, colonPos);
        String message = input.substring(colonPos + 1);
        uint8_t clientId = (uint8_t)strtol(idStr.c_str(), NULL, 16);
        broker.sendDirectMessage(clientId, message);
        Serial.print("Sent direct message to client 0x");
        Serial.println(clientId, HEX);
      } else {
        Serial.println("Usage: msg:clientId:message (clientId in hex)");
      }
    } else if (input.length() > 0) {
      Serial.println("Unknown command. Available commands:");
      Serial.println("  list, stats, pub:topic:msg, msg:id:msg");
    }
  }
}

// Callback when a client connects
void onClientConnect(uint8_t clientId) {
  Serial.print("Client connected: 0x");
  Serial.println(clientId, HEX);
}

// Callback when a message is published
void onPublish(uint16_t topicHash, const String& topic, const String& message) {
  Serial.print("Publish on [");
  Serial.print(topic);
  Serial.print(" (0x");
  Serial.print(topicHash, HEX);
  Serial.print(")]: ");
  Serial.println(message);
}

// Callback when a direct message is received
void onDirectMessage(uint8_t senderId, const String& message) {
  Serial.print("Direct message from 0x");
  Serial.print(senderId, HEX);
  Serial.print(": ");
  Serial.println(message);
}

// List all clients and their subscriptions
void listClientsAndSubscriptions() {
  Serial.println("\n=== Clients and Subscriptions ===");
  Serial.print("Connected clients: ");
  Serial.println(broker.getClientCount());
  Serial.print("Total subscriptions: ");
  Serial.println(broker.getSubscriptionCount());
  Serial.println();
  
  // Note: This is a simplified display
  // In a full implementation, you would iterate through all subscriptions
  Serial.println("Use 'topics' to see subscribed topics");
  Serial.println("Use 'stats' for detailed statistics");
}

// List all subscribed topics
void listTopics() {
  Serial.println("\n=== Subscribed Topics ===");
  
  if (broker.getSubscriptionCount() == 0) {
    Serial.println("No topics subscribed yet.");
    Serial.println();
    return;
  }
  
  Serial.println("Topic Name                       Hash      Subscribers");
  Serial.println("───────────────────────────────────────────────────────");
  
  broker.listSubscribedTopics([](uint16_t hash, const String& name, uint8_t count) {
    // Print topic name (pad to 32 chars)
    Serial.print(name);
    for (int i = name.length(); i < 32; i++) {
      Serial.print(" ");
    }
    
    // Print hash
    Serial.print("0x");
    if (hash < 0x1000) Serial.print("0");
    if (hash < 0x100) Serial.print("0");
    if (hash < 0x10) Serial.print("0");
    Serial.print(hash, HEX);
    Serial.print("    ");
    
    // Print subscriber count
    Serial.println(count);
  });
  
  Serial.println("───────────────────────────────────────────────────────");
  Serial.print("Total topics: ");
  Serial.println(broker.getSubscriptionCount());
  Serial.println();
}

// Show broker statistics
void showStats() {
  Serial.println("\n=== Broker Statistics ===");
  Serial.print("Connected clients: ");
  Serial.println(broker.getClientCount());
  Serial.print("Active topics: ");
  Serial.println(broker.getSubscriptionCount());
  Serial.print("Uptime: ");
  Serial.print(millis() / 1000);
  Serial.println(" seconds");
  Serial.println();
}

// Publish a message from the broker
void publishMessage(const String& topic, const String& message) {
  uint16_t topicHash = CANPubSubBase::hashTopic(topic);
  broker.registerTopic(topic);
  broker.broadcastMessage(topicHash, message);
  
  Serial.print("Published to [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
}
