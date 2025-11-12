/*
  CAN Pub/Sub Broker with Serial Number Management
  
  This example demonstrates a CAN bus publish/subscribe broker with advanced
  client ID management using serial numbers (MAC addresses, unique IDs, etc).
  
  Features:
  - Client registration with serial numbers
  - Persistent ID assignment (same serial = same ID)
  - FLASH MEMORY STORAGE - Mappings survive power outages!
  - Optional auto-ping for connection monitoring
  - Client disconnect detection on timeout
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
  - clients       - Show registered clients with online/offline status
  - topics        - List all subscribed topics
  - stats         - Show broker statistics
  - pub:topic:msg - Publish message to a topic
  - msg:id:msg    - Send direct message to client (ID in decimal)
  - unreg:id      - Unregister client by ID (decimal)
  - unreg:SN      - Unregister client by serial number
  - find:SN       - Find client ID by serial number
  - clear         - Clear all stored mappings (requires confirmation)
  - clearall      - Clear everything (mappings, subscriptions, topics, ping config)
  - ping:on       - Enable auto-ping
  - ping:off      - Disable auto-ping
  - interval:ms   - Set ping interval (minimum 1000ms)
  - maxmissed:n   - Set max missed pings (1-10)
  
  Created 2025
  by Juan Pablo Risso
*/

#include <SuperCANBus.h>

// Create broker instance
CANPubSubBroker broker(CAN);

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║  CAN Pub/Sub Broker with Serial Number Mgmt   ║");
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
  broker.onClientDisconnect(onClientDisconnect);
  broker.onPublish(onPublish);
  broker.onDirectMessage(onDirectMessage);
  
  // Optional: Enable auto-ping for connection monitoring
  // Uncomment these lines to enable automatic client health checks
  // broker.setPingInterval(5000);      // Ping every 5 seconds
  // broker.setMaxMissedPings(2);       // Mark inactive after 2 missed pings
  // broker.enableAutoPing(true);       // Enable automatic pinging
  
  Serial.println();
  Serial.println("Broker ready! (Mappings stored in flash memory)");
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  clients        - Show registered clients with online/offline status");
  Serial.println("  topics         - List subscribed topics");
  Serial.println("  sub:topic      - Subscribe broker to topic");
  Serial.println("  unsub:topic    - Unsubscribe broker from topic");
  Serial.println("  pub:topic:msg  - Publish to topic");
  Serial.println("  msg:id:msg     - Send direct message to client");
  Serial.println("  stats          - Show statistics");
  Serial.println("  unreg:id       - Unregister client by ID (decimal)");
  Serial.println("  unreg:SN       - Unregister by serial number");
  Serial.println("  find:SN        - Find client ID by serial");
  Serial.println("  clear          - Clear all stored mappings");
  Serial.println("  clearall       - Clear everything (mappings, subscriptions, topics)");
  Serial.println("  ping:on        - Enable auto-ping");
  Serial.println("  ping:off       - Disable auto-ping");
  Serial.println("  interval:ms    - Set ping interval (ms)");
  Serial.println("  maxmissed:n    - Set max missed pings");
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
    
    if (input == "clients") {
      listActiveClients();
      
    } else if (input == "topics") {
      listTopics();
      
    } else if (input == "stats") {
      showStats();
      
    } else if (input.startsWith("sub:")) {
      String topic = input.substring(4);
      subscribeBrokerToTopic(topic);
      
    } else if (input.startsWith("unsub:")) {
      String topic = input.substring(6);
      unsubscribeBrokerFromTopic(topic);
      
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
        uint8_t clientId = (uint8_t)idStr.toInt();
        
        if (clientId == 0) {
          Serial.println("✗ Error: Cannot send message to broker itself (ID 0)");
        } else {
          broker.sendDirectMessage(clientId, message);
          Serial.print("Sent direct message to client ");
          Serial.println(clientId, DEC);
        }
      } else {
        Serial.println("Usage: msg:clientId:message (clientId in decimal)");
      }
      
    } else if (input.startsWith("unreg:")) {
      String param = input.substring(6);
      unregisterClient(param);
      
    } else if (input.startsWith("find:")) {
      String serial = input.substring(5);
      findClient(serial);
      
    } else if (input == "clear") {
      clearAllMappings();
      
    } else if (input == "clearall") {
      clearEverything();
      
    } else if (input == "ping:on") {
      broker.enableAutoPing(true);
      Serial.println("Auto-ping enabled");
      
    } else if (input == "ping:off") {
      broker.enableAutoPing(false);
      Serial.println("Auto-ping disabled");
      
    } else if (input.startsWith("interval:")) {
      unsigned long interval = input.substring(9).toInt();
      if (interval >= 1000) {
        broker.setPingInterval(interval);
        Serial.print("Ping interval set to ");
        Serial.print(interval);
        Serial.println(" ms");
      } else {
        Serial.println("Interval must be >= 1000 ms");
      }
      
    } else if (input.startsWith("maxmissed:")) {
      uint8_t maxMissed = input.substring(10).toInt();
      if (maxMissed >= 1 && maxMissed <= 10) {
        broker.setMaxMissedPings(maxMissed);
        Serial.print("Max missed pings set to ");
        Serial.println(maxMissed);
      } else {
        Serial.println("Max missed must be 1-10");
      }
      
    } else if (input.length() > 0) {
      Serial.println("Unknown command. Type a command or see list above.");
    }
  }
}

// Callback when a client connects
void onClientConnect(uint8_t clientId) {
  String serial = broker.getSerialByClientId(clientId);
  
  Serial.print(">>> Client connected: ");
  Serial.print(clientId, DEC);
  if (serial.length() > 0) {
    Serial.print(" (SN: ");
    Serial.print(serial);
    Serial.print(")");
  }
  Serial.println();
}

// Callback when a client disconnects (timeout)
void onClientDisconnect(uint8_t clientId) {
  String serial = broker.getSerialByClientId(clientId);
  
  Serial.print(">>> Client disconnected: ");
  Serial.print(clientId, DEC);
  if (serial.length() > 0) {
    Serial.print(" (SN: ");
    Serial.print(serial);
    Serial.print(")");
  }
  Serial.println(" (timeout)");
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
  
  Serial.print(">>> Direct message from ");
  Serial.print(senderId, DEC);
  if (serial.length() > 0) {
    Serial.print(" (SN: ");
    Serial.print(serial);
    Serial.print(")");
  }
  Serial.print(": ");
  Serial.println(message);
}

// List active clients
void listActiveClients() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║         Registered Clients                    ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  
  uint8_t activeCount = 0;
  uint8_t onlineCount = 0;
  
  Serial.println("ID    Serial Number            Status    Subs");
  Serial.println("────────────────────────────────────────────────");
  
  broker.listRegisteredClients([&activeCount, &onlineCount](uint8_t id, const String& serial, bool registered) {
    // Only show clients that are registered (registered=true means stored in flash)
    if (registered) {
      Serial.print(id, DEC);
      if (id < 10) Serial.print("     ");
      else if (id < 100) Serial.print("    ");
      else Serial.print("   ");
      
      // Pad serial number to 24 chars
      Serial.print(serial);
      for (int i = serial.length(); i < 24; i++) {
        Serial.print(" ");
      }
      Serial.print(" ");
      
      // Check if client is online (currently connected)
      if (broker.isClientOnline(id)) {
        Serial.print("Online    ");
        onlineCount++;
      } else {
        Serial.print("Offline   ");
      }
      
      // Show subscription count
      uint8_t subCount = broker.getClientSubscriptionCount(id);
      Serial.println(subCount);
      
      activeCount++;
    }
  });
  
  Serial.println("────────────────────────────────────────────────");
  Serial.print("Registered: ");
  Serial.print(activeCount);
  Serial.print("  |  Online: ");
  Serial.println(onlineCount);
  Serial.println();
}

// List all subscribed topics
void listTopics() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║         Subscribed Topics                     ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  
  if (broker.getSubscriptionCount() == 0) {
    Serial.println("No topics subscribed yet.");
    Serial.println();
    return;
  }
  
  Serial.println("Topic Name                      Hash      Subscribers   Broker");
  Serial.println("──────────────────────────────────────────────────────────────");
  
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
    
    // Print subscriber count (pad to 12 chars)
    Serial.print(count);
    for (int i = String(count).length(); i < 12; i++) {
      Serial.print(" ");
    }
    
    // Check if broker (ID 0) is subscribed to this topic
    uint8_t subscribers[MAX_SUBSCRIBERS_PER_TOPIC];
    uint8_t subCount = 0;
    broker.getSubscribers(hash, subscribers, &subCount);
    
    bool brokerSubscribed = false;
    for (uint8_t i = 0; i < subCount; i++) {
      if (subscribers[i] == CAN_PS_BROKER_ID) {
        brokerSubscribed = true;
        break;
      }
    }
    
    Serial.println(brokerSubscribed ? "  Yes" : "  No");
  });
  
  Serial.println("─────────────────────────────────────────────────────────────");
  Serial.print("Total topics: ");
  Serial.println(broker.getSubscriptionCount());
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
  Serial.print("Online clients:      ");
  Serial.println(broker.getClientCount());
  Serial.print("Active topics:       ");
  Serial.println(broker.getSubscriptionCount());
  Serial.print("Auto-ping:           ");
  Serial.println(broker.isAutoPingEnabled() ? "Enabled" : "Disabled");
  if (broker.isAutoPingEnabled()) {
    Serial.print("  Interval:          ");
    Serial.print(broker.getPingInterval());
    Serial.println(" ms");
    Serial.print("  Max missed:        ");
    Serial.println(broker.getMaxMissedPings());
  }
  Serial.print("Uptime:              ");
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

// Unregister a client
void unregisterClient(const String& param) {
  // Try as decimal ID first (if all digits)
  bool isNumeric = true;
  for (unsigned int i = 0; i < param.length(); i++) {
    if (!isDigit(param.charAt(i))) {
      isNumeric = false;
      break;
    }
  }
  
  if (isNumeric && param.length() > 0 && param.length() <= 3) {
    uint8_t clientId = (uint8_t)param.toInt();
    if (broker.unregisterClient(clientId)) {
      Serial.print("Unregistered client ");
      Serial.println(clientId, DEC);
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
  
  if (clientId != CAN_PS_UNASSIGNED_ID) {
    Serial.print("Client ID for serial '");
    Serial.print(serial);
    Serial.print("': ");
    Serial.println(clientId, DEC);
  } else {
    Serial.print("No client found with serial: ");
    Serial.println(serial);
  }
}

// Clear everything (mappings, subscriptions, topics, ping config)
void clearEverything() {
  Serial.println("⚠ WARNING: This will clear ALL stored data:");
  Serial.println("  - Client mappings");
  Serial.println("  - Subscriptions");
  Serial.println("  - Topic names");
  Serial.println("  - Ping configuration");
  Serial.println("Type 'YES' to confirm or anything else to cancel:");
  
  // Wait for confirmation (with timeout)
  unsigned long startTime = millis();
  while (!Serial.available() && (millis() - startTime) < 10000);
  
  if (Serial.available()) {
    String confirm = Serial.readStringUntil('\n');
    confirm.trim();
    
    if (confirm == "YES") {
      broker.clearStoredMappings();
      broker.clearStoredSubscriptions();
      broker.clearStoredTopicNames();
      broker.clearStoredPingConfig();
      
      Serial.println("✓ All data cleared from flash memory");
      Serial.println("ⓘ Restarting...");
      Serial.flush();
      delay(100);
      
      #if defined(ESP32) || defined(ESP8266)
        ESP.restart();
      #else
        // For Arduino boards, use software reset via watchdog
        void(* resetFunc) (void) = 0; // Declare reset function at address 0
        resetFunc(); // Call reset
      #endif
    } else {
      Serial.println("Cancelled");
    }
  } else {
    Serial.println("Timeout - cancelled");
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
        Serial.println("ⓘ Restarting...");
        Serial.flush();
        delay(100);
        
        #if defined(ESP32) || defined(ESP8266)
          ESP.restart();
        #else
          // For Arduino boards, use software reset via watchdog
          void(* resetFunc) (void) = 0; // Declare reset function at address 0
          resetFunc(); // Call reset
        #endif
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

// Subscribe broker to a topic
void subscribeBrokerToTopic(const String& topic) {
  uint16_t topicHash = CANPubSubBase::hashTopic(topic);
  broker.registerTopic(topic);
  
  // Send subscribe message as if broker is subscribing
  CAN.beginPacket(CAN_PS_SUBSCRIBE);
  CAN.write(CAN_PS_BROKER_ID);  // Broker ID is 0
  CAN.write(topicHash >> 8);
  CAN.write(topicHash & 0xFF);
  CAN.endPacket();
  
  Serial.print("Broker subscribed to [");
  Serial.print(topic);
  Serial.println("]");
}

// Unsubscribe broker from a topic
void unsubscribeBrokerFromTopic(const String& topic) {
  uint16_t topicHash = CANPubSubBase::hashTopic(topic);
  
  // Send unsubscribe message as if broker is unsubscribing
  CAN.beginPacket(CAN_PS_UNSUBSCRIBE);
  CAN.write(CAN_PS_BROKER_ID);  // Broker ID is 0
  CAN.write(topicHash >> 8);
  CAN.write(topicHash & 0xFF);
  CAN.endPacket();
  
  Serial.print("Broker unsubscribed from [");
  Serial.print(topic);
  Serial.println("]");
}
