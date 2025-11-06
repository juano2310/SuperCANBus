/*
  CAN MQTT Client with Serial Number
  
  This example demonstrates a CAN bus MQTT client that registers
  with the broker using a unique serial number. The broker will
  assign the same ID to this client every time it connects.
  
  Features:
  - Unique serial number identification
  - Persistent ID across reconnections
  - All standard MQTT client features
  - Serial command interface
  
  Circuit:
  - MCP2515 CAN module connected to SPI pins
  - Or ESP32 with built-in CAN controller
  
  Commands (via Serial):
  - sub:topic      - Subscribe to a topic
  - unsub:topic    - Unsubscribe from topic
  - pub:topic:msg  - Publish message to topic
  - msg:message    - Send direct message to broker
  - ping           - Ping the broker
  - status         - Show connection status
  - serial         - Show serial number and ID
  
  Created 2025
  by Juan Pablo Risso
*/

#include <SUPER_CAN.h>

// Create client instance
CANMqttClient client(CAN);

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
  Serial.println("║  CAN MQTT Client with Serial Number          ║");
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
  
  // Connect to broker with serial number
  Serial.println("Connecting to broker...");
  if (client.begin(SERIAL_NUMBER, 5000)) {
    Serial.print("✓ Connected! Client ID: 0x");
    Serial.println(client.getClientId(), HEX);
    wasConnected = true;
  } else {
    Serial.println("× Failed to connect to broker!");
    Serial.println("  Make sure the broker is running on the CAN bus.");
  }
  
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  sub:topic      - Subscribe to topic");
  Serial.println("  unsub:topic    - Unsubscribe from topic");
  Serial.println("  pub:topic:msg  - Publish to topic");
  Serial.println("  msg:message    - Send direct message to broker");
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
      String message = input.substring(4);
      if (client.sendDirectMessage(message)) {
        Serial.print("✓ Sent to broker: ");
        Serial.println(message);
      } else {
        Serial.println("× Failed to send message");
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

// Callback when a direct message is received
void onDirectMessage(uint8_t senderId, const String& message) {
  Serial.print(">>> Direct from 0x");
  Serial.print(senderId, HEX);
  Serial.print(": ");
  Serial.println(message);
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
    Serial.print("Client ID:      0x");
    Serial.println(client.getClientId(), HEX);
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
    Serial.print("Assigned ID:    0x");
    Serial.println(client.getClientId(), HEX);
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
