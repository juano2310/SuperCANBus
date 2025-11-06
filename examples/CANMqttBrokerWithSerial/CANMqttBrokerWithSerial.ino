/*
  CAN MQTT Broker with Serial Number Management
  
  This example demonstrates a CAN bus MQTT-like broker with advanced
  client ID management using serial numbers (MAC addresses, unique IDs, etc).
  
  Features:
  - Client registration with serial numbers
  - Persistent ID assignment (same serial = same ID)
  - FLASH MEMORY STORAGE - Mappings survive power outages!
  - Client management (add, edit, remove)
  - Serial command interface for administration
  
  Storage:
  - ESP32: Uses Preferences (NVS) for persistent storage
  - Arduino: Uses EEPROM for persistent storage
  - Mappings automatically saved on changes
  - Survives power cycles and resets
  
  Circuit:
  - MCP2515 CAN module connected to SPI pins
  - Or ESP32 with built-in CAN controller
  
  Commands (via Serial):
  - list          - List all registered clients
  - clients       - Show active clients
  - pub:topic:msg - Publish message to a topic
  - msg:id:msg    - Send direct message to client
  - stats         - Show broker statistics
  - unreg:id      - Unregister client by ID (hex)
  - unreg:SN      - Unregister client by serial number
  - find:SN       - Find client ID by serial number
  - clear         - Clear all stored mappings
  
  Created 2025
  by Juan Pablo Risso
*/

#include <SUPER_CAN.h>

// Create broker instance
CANMqttBroker broker(CAN);

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║  CAN MQTT Broker with Serial Number Mgmt      ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  
  Serial.println("Initializing CAN bus...");

  // Initialize CAN bus at 500 kbps
  if (!CAN.begin(500E3)) {
    Serial.println("ERROR: Starting CAN failed!");
    while (1);
  }
  
  Serial.println("✓ CAN bus initialized");
  
  // Initialize broker
  if (!broker.begin()) {
    Serial.println("ERROR: Starting broker failed!");
    while (1);
  }
  
  Serial.println("✓ Broker started successfully");
  
  // Check if mappings were loaded from storage
  uint8_t loadedCount = broker.getRegisteredClientCount();
  if (loadedCount > 0) {
    Serial.print("✓ Loaded ");
    Serial.print(loadedCount);
    Serial.println(" client mapping(s) from flash memory");
  } else {
    Serial.println("ⓘ No stored mappings found (fresh start)");
  }
  
  // Setup callbacks
  broker.onClientConnect(onClientConnect);
  broker.onPublish(onPublish);
  broker.onDirectMessage(onDirectMessage);
  
  Serial.println();
  Serial.println("Broker ready! (Mappings stored in flash memory)");
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  list           - List all registered clients");
  Serial.println("  clients        - Show active clients");
  Serial.println("  pub:topic:msg  - Publish to topic");
  Serial.println("  msg:id:msg     - Send direct message to client");
  Serial.println("  stats          - Show statistics");
  Serial.println("  unreg:id       - Unregister client by ID (hex)");
  Serial.println("  unreg:SN       - Unregister by serial number");
  Serial.println("  find:SN        - Find client ID by serial");
  Serial.println("  clear          - Clear all stored mappings");
  Serial.println("─────────────────────────────────────────────────");
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
      listAllClients();
      
    } else if (input == "clients") {
      listActiveClients();
      
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
      
    } else if (input.startsWith("unreg:")) {
      String param = input.substring(6);
      unregisterClient(param);
      
    } else if (input.startsWith("find:")) {
      String serial = input.substring(5);
      findClient(serial);
      
    } else if (input == "clear") {
      clearAllMappings();
      
    } else if (input.length() > 0) {
      Serial.println("Unknown command. Type a command or see list above.");
    }
  }
}

// Callback when a client connects
void onClientConnect(uint8_t clientId) {
  String serial = broker.getSerialByClientId(clientId);
  
  Serial.print(">>> Client connected: 0x");
  Serial.print(clientId, HEX);
  if (serial.length() > 0) {
    Serial.print(" (SN: ");
    Serial.print(serial);
    Serial.print(")");
  }
  Serial.println();
}

// Callback when a message is published
void onPublish(uint16_t topicHash, const String& topic, const String& message) {
  Serial.print(">>> Publish on [");
  Serial.print(topic);
  Serial.print(" (0x");
  Serial.print(topicHash, HEX);
  Serial.print(")]: ");
  Serial.println(message);
}

// Callback when a direct message is received
void onDirectMessage(uint8_t senderId, const String& message) {
  String serial = broker.getSerialByClientId(senderId);
  
  Serial.print(">>> Direct message from 0x");
  Serial.print(senderId, HEX);
  if (serial.length() > 0) {
    Serial.print(" (SN: ");
    Serial.print(serial);
    Serial.print(")");
  }
  Serial.print(": ");
  Serial.println(message);
}

// List all registered clients
void listAllClients() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║         Registered Clients                    ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  
  uint8_t count = broker.getRegisteredClientCount();
  
  if (count == 0) {
    Serial.println("No clients registered yet.");
  } else {
    Serial.println("ID    Serial Number                 Status");
    Serial.println("────────────────────────────────────────────────");
    
    broker.listRegisteredClients([](uint8_t id, const String& serial, bool active) {
      Serial.print("0x");
      if (id < 0x10) Serial.print("0");
      Serial.print(id, HEX);
      Serial.print("  ");
      
      // Pad serial number
      Serial.print(serial);
      for (int i = serial.length(); i < 28; i++) {
        Serial.print(" ");
      }
      
      Serial.println(active ? "Active" : "Inactive");
    });
    
    Serial.println("────────────────────────────────────────────────");
    Serial.print("Total registered: ");
    Serial.println(count);
  }
  
  Serial.println();
}

// List active clients
void listActiveClients() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║         Active Clients                        ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  
  uint8_t activeCount = 0;
  
  Serial.println("ID    Serial Number                 Subscriptions");
  Serial.println("────────────────────────────────────────────────");
  
  broker.listRegisteredClients([&activeCount](uint8_t id, const String& serial, bool active) {
    if (active) {
      Serial.print("0x");
      if (id < 0x10) Serial.print("0");
      Serial.print(id, HEX);
      Serial.print("  ");
      
      // Pad serial number
      Serial.print(serial);
      for (int i = serial.length(); i < 28; i++) {
        Serial.print(" ");
      }
      
      Serial.println("-"); // Subscription count could be added
      activeCount++;
    }
  });
  
  Serial.println("────────────────────────────────────────────────");
  Serial.print("Active clients: ");
  Serial.println(activeCount);
  Serial.println();
}

// Show broker statistics
void showStats() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║         Broker Statistics                     ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  Serial.print("Registered clients:  ");
  Serial.println(broker.getRegisteredClientCount());
  Serial.print("Active clients:      ");
  Serial.println(broker.getClientCount());
  Serial.print("Active topics:       ");
  Serial.println(broker.getSubscriptionCount());
  Serial.print("Uptime:              ");
  Serial.print(millis() / 1000);
  Serial.println(" seconds");
  Serial.println();
}

// Publish a message from the broker
void publishMessage(const String& topic, const String& message) {
  uint16_t topicHash = CANMqttBase::hashTopic(topic);
  broker.registerTopic(topic);
  broker.broadcastMessage(topicHash, message);
  
  Serial.print("Published to [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
}

// Unregister a client
void unregisterClient(const String& param) {
  // Try as hex ID first
  if (param.startsWith("0x") || param.length() <= 2) {
    uint8_t clientId = (uint8_t)strtol(param.c_str(), NULL, 16);
    if (broker.unregisterClient(clientId)) {
      Serial.print("Unregistered client 0x");
      Serial.println(clientId, HEX);
    } else {
      Serial.println("Client not found");
    }
  } else {
    // Try as serial number
    if (broker.unregisterClientBySerial(param)) {
      Serial.print("Unregistered client with serial: ");
      Serial.println(param);
    } else {
      Serial.println("Client not found");
    }
  }
}

// Find client by serial number
void findClient(const String& serial) {
  uint8_t clientId = broker.getClientIdBySerial(serial);
  
  if (clientId != CAN_MQTT_UNASSIGNED_ID) {
    Serial.print("Client ID for serial '");
    Serial.print(serial);
    Serial.print("': 0x");
    Serial.println(clientId, HEX);
  } else {
    Serial.print("No client found with serial: ");
    Serial.println(serial);
  }
}

// Clear all stored mappings
void clearAllMappings() {
  Serial.print("⚠ WARNING: This will clear all ");
  Serial.print(broker.getRegisteredClientCount());
  Serial.println(" stored client mappings!");
  Serial.println("Type 'YES' to confirm or anything else to cancel:");
  
  // Wait for confirmation (with timeout)
  unsigned long startTime = millis();
  while (!Serial.available() && (millis() - startTime) < 10000);
  
  if (Serial.available()) {
    String confirm = Serial.readStringUntil('\n');
    confirm.trim();
    
    if (confirm == "YES") {
      if (broker.clearStoredMappings()) {
        Serial.println("✓ All mappings cleared from flash memory");
        Serial.println("ⓘ Broker reset to fresh state");
      } else {
        Serial.println("✗ Failed to clear mappings");
      }
    } else {
      Serial.println("✗ Operation cancelled");
    }
  } else {
    Serial.println("✗ Timeout - operation cancelled");
  }
}
