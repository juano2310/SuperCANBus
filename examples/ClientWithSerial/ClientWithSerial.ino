/*
  CAN Pub/Sub Client with Serial Number
  
  This example demonstrates a CAN bus publish/subscribe client that registers
  with the broker using a unique serial number. The broker will
  assign the same ID to this client every time it connects.
  
  Features:
  - Unique serial number identification
  - Persistent ID across reconnections
  - 🔄 AUTOMATIC SUBSCRIPTION RESTORATION - No need to re-subscribe!
  - All standard pub/sub client features
  - Serial command interface
  
  Circuit:
  - MCP2515 CAN module connected to SPI pins
  - Or ESP32 with built-in CAN controller
  
  Commands (via Serial):
  - sub:topic      - Subscribe to a topic
  - unsub:topic    - Unsubscribe from topic
  - pub:topic:msg  - Publish message to topic
  - msg:0:message  - Send direct message to broker (ID 0)
  - msg:id:message - Send peer message to client ID
  - ping           - Ping the broker
  - status         - Show connection status
  - serial         - Show serial number and ID
  - topics         - List subscriptions
  
  Created 2025
  by Juan Pablo Risso
*/

#include <SuperCANBus.h>

// Create client instance
CANPubSubClient client(CAN);

// ===== CONFIGURATION =====
// Set your unique serial number here
// This could be:
// - MAC address: "00:1A:2B:3C:4D:5E"
// - UUID: "550e8400-e29b-41d4-a716"
// - Custom ID: "SENSOR_001"
// - ESP32 chip ID: String(ESP.getEfuseMac(), HEX)

String SERIAL_NUMBER = "NODE_" + String(random(1000, 9999));

// ===== END CONFIGURATION =====

bool wasConnected = false;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║  CAN Pub/Sub Client with Serial Number        ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  
  // Generate unique serial if using ESP32
  #ifdef ESP32
    uint64_t chipid = ESP.getEfuseMac();
    SERIAL_NUMBER = "ESP32_" + String((uint32_t)(chipid >> 32), HEX) + 
                                String((uint32_t)chipid, HEX);
    SERIAL_NUMBER.toUpperCase();
  #endif
  
  Serial.print("Serial Number: ");
  Serial.println(SERIAL_NUMBER);
  Serial.println();
  
  Serial.println("Initializing CAN bus...");

  // Initialize CAN bus at 500 kbps
  if (!CAN.begin(500E3)) {
    Serial.println("ERROR: Starting CAN failed!");
    while (1);
  }
  
  Serial.println("✓ CAN bus initialized");
  
  // Setup callbacks before connecting
  client.onConnect(onConnect);
  client.onMessage(onMessage);
  client.onDirectMessage(onDirectMessage);
  client.onPong(onPong);
  
  // Connect to broker with serial number
  Serial.println("Connecting to broker...");
  if (client.begin(SERIAL_NUMBER, 5000)) {
    Serial.print("✓ Connected! Client ID: ");
    Serial.println(client.getClientId(), DEC);
    
    // Check if subscriptions were restored
    uint8_t subCount = client.getSubscriptionCount();
    if (subCount > 0) {
      Serial.println();
      Serial.println("🔄 Subscriptions automatically restored from broker!");
      Serial.print("   Restored ");
      Serial.print(subCount);
      Serial.print(" subscription(s): ");
      
      // List restored subscriptions
      bool first = true;
      client.listSubscribedTopics([&first](uint16_t hash, const String& name) {
        if (!first) Serial.print(", ");
        Serial.print(name);
        first = false;
      });
      Serial.println();
      Serial.println("   ℹ️  No need to re-subscribe manually!");
    }
    
    wasConnected = true;
  } else {
    Serial.println("× Failed to connect to broker!");
    Serial.println("  Make sure the broker is running on the CAN bus.");
  }
  
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  topics         - List subscriptions");
  Serial.println("  sub:topic      - Subscribe to topic");
  Serial.println("  unsub:topic    - Unsubscribe from topic");
  Serial.println("  pub:topic:msg  - Publish to topic");
  Serial.println("  msg:0:message  - Send to broker (ID 0)");
  Serial.println("  msg:id:message - Send peer message to client");
  Serial.println("  ping           - Ping broker");
  Serial.println("  status         - Show connection status");
  Serial.println("  serial         - Show serial number and ID");
  Serial.println("─────────────────────────────────────────────────");
  Serial.println();
}

void loop() {
  // Check connection status
  if (client.isConnected() && !wasConnected) {
    Serial.println(">>> Connected to broker!");
    wasConnected = true;
  } else if (!client.isConnected() && wasConnected) {
    Serial.println(">>> Disconnected from broker!");
    Serial.println("    Attempting to reconnect...");
    if (client.connect(SERIAL_NUMBER, 5000)) {
      Serial.println("✓ Reconnected!");
      
      // Check if subscriptions were restored
      uint8_t subCount = client.getSubscriptionCount();
      if (subCount > 0) {
        Serial.print("🔄 Restored ");
        Serial.print(subCount);
        Serial.println(" subscription(s) automatically");
      }
    }
    wasConnected = false;
  }
  
  // Process incoming CAN messages
  client.loop();
  
  // Handle serial commands
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (!client.isConnected() && input != "status" && input != "serial") {
      Serial.println("Not connected to broker!");
      return;
    }
    
    if (input.startsWith("sub:")) {
      String topic = input.substring(4);
      if (client.subscribe(topic)) {
        Serial.print("✓ Subscribed to: ");
        Serial.println(topic);
      } else {
        Serial.println("× Failed to subscribe");
      }
      
    } else if (input.startsWith("unsub:")) {
      String topic = input.substring(6);
      if (client.unsubscribe(topic)) {
        Serial.print("✓ Unsubscribed from: ");
        Serial.println(topic);
      } else {
        Serial.println("× Failed to unsubscribe");
      }
      
    } else if (input.startsWith("pub:")) {
      int colonPos = input.indexOf(':', 4);
      if (colonPos > 0) {
        String topic = input.substring(4, colonPos);
        String message = input.substring(colonPos + 1);
        if (client.publish(topic, message)) {
          Serial.print("✓ Published to [");
          Serial.print(topic);
          Serial.print("]: ");
          Serial.println(message);
        } else {
          Serial.println("× Failed to publish");
        }
      } else {
        Serial.println("Usage: pub:topic:message");
      }
      
    } else if (input.startsWith("msg:")) {
      int colonPos = input.indexOf(':', 4);
      if (colonPos > 0) {
        uint8_t targetId = input.substring(4, colonPos).toInt();
        String message = input.substring(colonPos + 1);
        
        if (targetId == 0) {
          // Send to broker
          if (client.sendDirectMessage(message)) {
            Serial.print("✓ Sent to broker: ");
            Serial.println(message);
          } else {
            Serial.println("× Failed to send message");
          }
        } else {
          // Send peer-to-peer
          if (client.sendPeerMessage(targetId, message)) {
            Serial.print("✓ Peer message sent to client ");
            Serial.print(targetId);
            Serial.print(": ");
            Serial.println(message);
          } else {
            Serial.println("× Failed to send peer message");
            Serial.println("  Note: Both clients must have permanent IDs (< 101)");
          }
        }
      } else {
        Serial.println("Usage: msg:id:message (0 for broker, >0 for peers)");
      }
      
    } else if (input == "ping") {
      if (client.ping()) {
        Serial.println("✓ Ping sent to broker");
      } else {
        Serial.println("× Failed to send ping");
      }
      
    } else if (input == "status") {
      showStatus();
      
    } else if (input == "serial") {
      showSerialInfo();
      
    } else if (input == "topics") {
      listSubscriptions();
      
    } else if (input.length() > 0) {
      Serial.println("Unknown command. Type 'status' for help.");
    }
  }
}

// Callback when connected to broker
void onConnect() {
  Serial.println(">>> Connection established!");
}

// Callback when a message is received on a subscribed topic
void onMessage(uint16_t topicHash, const String& topic, const String& message) {
  Serial.print(">>> [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
}

// Callback when a direct message is received (from broker or peer)
void onDirectMessage(uint8_t senderId, const String& message) {
  if (senderId == 0) {
    Serial.print(">>> Broker: ");
  } else if (senderId == client.getClientId()) {
    Serial.print(">>> Self (ID ");
    Serial.print(senderId, DEC);
    Serial.print("): ");
  } else {
    Serial.print(">>> Peer (ID ");
    Serial.print(senderId, DEC);
    Serial.print("): ");
  }
  Serial.println(message);
}

// Callback when broker pong is received
void onPong() {
  unsigned long rtt = client.getLastPingTime();
  Serial.print(">>> Pong from broker [");
  Serial.print(rtt);
  Serial.println("ms]");
}

// Show connection status
void showStatus() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║         Client Status                         ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  Serial.print("Connected:      ");
  Serial.println(client.isConnected() ? "Yes" : "No");
  
  if (client.isConnected()) {
    Serial.print("Client ID:      ");
    Serial.println(client.getClientId(), DEC);
    Serial.print("Serial Number:  ");
    Serial.println(SERIAL_NUMBER);
    Serial.print("Subscriptions:  ");
    Serial.println(client.getSubscriptionCount());
  }
  
  Serial.print("Uptime:         ");
  Serial.print(millis() / 1000);
  Serial.println(" seconds");
  Serial.println();
}

// Show serial number and ID information
void showSerialInfo() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║         Serial Number Information             ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  Serial.print("Serial Number:  ");
  Serial.println(SERIAL_NUMBER);
  
  if (client.isConnected()) {
    Serial.print("Assigned ID:    ");
    Serial.println(client.getClientId(), DEC);
    Serial.println();
    Serial.println("Note: This ID will remain the same across");
    Serial.println("      reconnections as long as the serial");
    Serial.println("      number doesn't change.");
  } else {
    Serial.println("Status:         Not connected");
    Serial.println();
    Serial.println("Connect to the broker to receive an ID.");
  }
  
  Serial.println();
}

// List all subscribed topics
void listSubscriptions() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║         Subscribed Topics                     ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  
  uint8_t count = client.getSubscriptionCount();
  if (count == 0) {
    Serial.println("No active subscriptions.");
    Serial.println();
    return;
  }
  
  Serial.println("Topic Name                       Hash");
  Serial.println("─────────────────────────────────────────────────");
  
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
  
  Serial.println("─────────────────────────────────────────────────");
  Serial.print("Total subscriptions: ");
  Serial.println(count);
  Serial.println();
}
