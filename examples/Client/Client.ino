/*
  CAN Pub/Sub Client Example
  
  This example demonstrates how to create a CAN bus publish/subscribe client
  that can subscribe to topics, publish messages, and communicate with a broker.
  
  Features:
  - Automatic connection to broker with ID assignment
  - Topic subscription/unsubscription
  - Message publishing
  - Direct messaging to broker
  - Connection status monitoring
  - Serial command interface
  
  Note: This client uses temporary IDs (101+). For persistent IDs and
        automatic subscription restoration, see ClientWithSerial.ino or
        SubscriptionRestore.ino examples
  
  Circuit:
  - MCP2515 CAN module connected to SPI pins
  - Or ESP32 with built-in CAN controller
  
  Commands (via Serial):
  - topics         - List subscribed topics
  - sub:topic      - Subscribe to a topic
  - unsub:topic    - Unsubscribe from topic
  - pub:topic:msg  - Publish message to topic
  - msg:message    - Send direct message to broker
  - ping           - Ping the broker
  - status         - Show connection status
  
  Created 2025
  by Juan Pablo Risso
*/

#include <SUPER_CAN.h>

// Create client instance
CANPubSubClient client(CAN);

// Connection status
bool wasConnected = false;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("=== CAN Pub/Sub Client ===");
  Serial.println("Initializing CAN bus...");

  // Initialize CAN bus at 500 kbps
  if (!CAN.begin(500E3)) {
    Serial.println("Starting CAN failed!");
    while (1);
  }
  
  Serial.println("CAN bus initialized");
  
  // Setup callbacks before connecting
  client.onConnect(onConnect);
  client.onMessage(onMessage);
  client.onDirectMessage(onDirectMessage);
  client.onPong(onPong);
  
  // Connect to broker
  Serial.println("Connecting to broker...");
  if (client.begin(5000)) {
    Serial.print("Connected! Client ID: ");
    Serial.println(client.getClientId(), DEC);
    Serial.println("(Temporary ID - will change on reconnect)");
    Serial.println();
    Serial.println("💡 Tip: Use ClientWithSerial.ino for persistent ID");
    Serial.println("   and automatic subscription restoration!");
    wasConnected = true;
  } else {
    Serial.println("Failed to connect to broker!");
    Serial.println("Make sure the broker is running on the CAN bus.");
  }
  
  Serial.println("\nCommands:");
  Serial.println("  sub:topic      - Subscribe to topic");
  Serial.println("  unsub:topic    - Unsubscribe from topic");
  Serial.println("  pub:topic:msg  - Publish to topic");
  Serial.println("  msg:message    - Send direct message to broker");
  Serial.println("  ping           - Ping broker");
  Serial.println("  status         - Show connection status");
  Serial.println("  topics         - List subscriptions");
  Serial.println();
}

void loop() {
  // Check connection status
  if (client.isConnected() && !wasConnected) {
    Serial.println("Connected to broker!");
    wasConnected = true;
  } else if (!client.isConnected() && wasConnected) {
    Serial.println("Disconnected from broker!");
    wasConnected = false;
  }
  
  // Process incoming CAN messages
  client.loop();
  
  // Handle serial commands
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (!client.isConnected() && input != "status") {
      Serial.println("Not connected to broker!");
      return;
    }
    
    if (input.startsWith("sub:")) {
      String topic = input.substring(4);
      if (client.subscribe(topic)) {
        Serial.print("Subscribed to: ");
        Serial.println(topic);
      } else {
        Serial.println("Failed to subscribe");
      }
      
    } else if (input.startsWith("unsub:")) {
      String topic = input.substring(6);
      if (client.unsubscribe(topic)) {
        Serial.print("Unsubscribed from: ");
        Serial.println(topic);
      } else {
        Serial.println("Failed to unsubscribe");
      }
      
    } else if (input.startsWith("pub:")) {
      int colonPos = input.indexOf(':', 4);
      if (colonPos > 0) {
        String topic = input.substring(4, colonPos);
        String message = input.substring(colonPos + 1);
        if (client.publish(topic, message)) {
          Serial.print("Published to [");
          Serial.print(topic);
          Serial.print("]: ");
          Serial.println(message);
        } else {
          Serial.println("Failed to publish");
        }
      } else {
        Serial.println("Usage: pub:topic:message");
      }
      
    } else if (input.startsWith("msg:")) {
      String message = input.substring(4);
      if (client.sendDirectMessage(message)) {
        Serial.print("Sent to broker: ");
        Serial.println(message);
      } else {
        Serial.println("Failed to send message");
      }
      
    } else if (input == "ping") {
      if (client.ping()) {
        Serial.println("Ping sent to broker");
      } else {
        Serial.println("Failed to send ping");
      }
      
    } else if (input == "status") {
      showStatus();
      
    } else if (input == "topics") {
      listSubscriptions();
      
    } else if (input.length() > 0) {
      Serial.println("Unknown command. Type 'status' for help.");
    }
  }
}

// Callback when connected to broker
void onConnect() {
  Serial.println("Connection established!");
}

// Callback when a message is received on a subscribed topic
void onMessage(uint16_t topicHash, const String& topic, const String& message) {
  Serial.print("Received on [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
}

// Callback when a direct message is received
void onDirectMessage(uint8_t senderId, const String& message) {
  if (senderId == client.getClientId()) {
    Serial.print("Self message from ID ");
  } else {
    Serial.print("Direct message from ");
  }
  Serial.print(senderId, DEC);
  Serial.print(": ");
  Serial.println(message);
}

// Callback when broker pong is received
void onPong() {
  unsigned long rtt = client.getLastPingTime();
  Serial.print(">>> Pong received from broker [");
  Serial.print(rtt);
  Serial.println("ms]");
}

// Show connection status
void showStatus() {
  Serial.println("\n=== Client Status ===");
  Serial.print("Connected: ");
  Serial.println(client.isConnected() ? "Yes" : "No");
  
  if (client.isConnected()) {
    Serial.print("Client ID: ");
    Serial.println(client.getClientId(), DEC);
    Serial.print("Subscriptions: ");
    Serial.println(client.getSubscriptionCount());
  }
  
  Serial.print("Uptime: ");
  Serial.print(millis() / 1000);
  Serial.println(" seconds");
  Serial.println();
}

// List all subscribed topics
void listSubscriptions() {
  Serial.println("\n=== Subscribed Topics ===");
  
  uint8_t count = client.getSubscriptionCount();
  if (count == 0) {
    Serial.println("No active subscriptions.");
    Serial.println();
    return;
  }
  
  Serial.println("Topic Name                       Hash");
  Serial.println("─────────────────────────────────────────────");
  
  client.listSubscribedTopics([](uint16_t hash, const String& name) {
    // Print topic name (pad to 32 chars)
    Serial.print(name);
    for (int i = name.length(); i < 32; i++) {
      Serial.print(" ");
    }
    Serial.print(" 0x");
    if (hash < 0x1000) Serial.print("0");
    if (hash < 0x100) Serial.print("0");
    if (hash < 0x10) Serial.print("0");
    Serial.println(hash, HEX);
  });
  
  Serial.println("─────────────────────────────────────────────");
  Serial.print("Total subscriptions: ");
  Serial.println(count);
  Serial.println();
}
