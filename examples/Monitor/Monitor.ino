/*
  CAN PubSub Network Monitor
  
  This example creates a diagnostic tool that monitors all traffic
  on the CAN PubSub network without participating as a broker or client.
  
  Features:
  - Monitors all CAN PubSub message types
  - Displays client connections and subscriptions
  - Shows topic activity and message content
  - Provides network statistics
  - Useful for debugging and network analysis
  
  Circuit:
  - MCP2515 CAN module connected to SPI pins
  - Or ESP32 with built-in CAN controller
  
  Note: This is a passive monitor that listens to all CAN traffic
  but does not interfere with the network.
  
  Created 2025
  by Juan Pablo Risso
*/

#include <SuperCANBus.h>

// Statistics
unsigned long totalMessages = 0;
unsigned long subscribeCount = 0;
unsigned long unsubscribeCount = 0;
unsigned long publishCount = 0;
unsigned long directMsgCount = 0;
unsigned long pingCount = 0;
unsigned long idRequestCount = 0;
unsigned long startTime = 0;

// Topic tracking
struct TopicActivity {
  uint16_t hash;
  unsigned long count;
  String name;
};

TopicActivity topics[20];
uint8_t topicCount = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("╔═══════════════════════════════════════════╗");
  Serial.println("║    CAN PubSub Network Monitor v1.0        ║");
  Serial.println("╚═══════════════════════════════════════════╝");
  Serial.println();
  
  Serial.println("Initializing CAN bus...");

  // Initialize CAN bus at 500 kbps
  if (!CAN.begin(500E3)) {
    Serial.println("ERROR: Starting CAN failed!");
    while (1);
  }
  
  Serial.println("✓ CAN bus initialized at 500 kbps");
  
  // Register receive callback
  CAN.onReceive(onReceive);
  
  Serial.println("✓ Monitor active");
  Serial.println();
  Serial.println("Monitoring CAN PubSub traffic...");
  Serial.println("Press 's' for statistics, 'r' to reset, 't' for topics");
  Serial.println("─────────────────────────────────────────────");
  Serial.println();
  
  startTime = millis();
}

void loop() {
  // Handle serial commands
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 's' || cmd == 'S') {
      printStatistics();
    } else if (cmd == 'r' || cmd == 'R') {
      resetStatistics();
    } else if (cmd == 't' || cmd == 'T') {
      printTopics();
    } else if (cmd == 'h' || cmd == 'H') {
      printHelp();
    }
  }
  
  delay(10);
}

void onReceive(int packetSize) {
  totalMessages++;
  
  uint8_t msgType = CAN.packetId();
  String msgTypeName = getMessageTypeName(msgType);
  
  // Read packet data
  uint8_t data[8];
  int len = 0;
  while (CAN.available() && len < 8) {
    data[len++] = CAN.read();
  }
  
  // Display message
  Serial.print("[");
  printTime();
  Serial.print("] ");
  
  Serial.print(msgTypeName);
  Serial.print(" (0x");
  if (msgType < 0x10) Serial.print("0");
  Serial.print(msgType, HEX);
  Serial.print(")");
  
  // Parse and display message details
  switch (msgType) {
    case 0x01: // SUBSCRIBE
      handleSubscribe(data, len);
      subscribeCount++;
      break;
      
    case 0x02: // UNSUBSCRIBE
      handleUnsubscribe(data, len);
      unsubscribeCount++;
      break;
      
    case 0x03: // PUBLISH
      handlePublish(data, len);
      publishCount++;
      break;
      
    case 0x04: // TOPIC_DATA
      handleTopicData(data, len);
      break;
      
    case 0x05: // DIRECT_MSG
      handleDirectMsg(data, len);
      directMsgCount++;
      break;
      
    case 0x06: // PING
      handlePing(data, len);
      pingCount++;
      break;
      
    case 0x07: // PONG
      handlePong(data, len);
      break;
      
    case 0x08: // ACK
      handleAck(data, len);
      break;
      
    case 0xFF: // ID_REQUEST
      Serial.println(" - Client requesting ID");
      idRequestCount++;
      break;
      
    case 0xFE: // ID_RESPONSE
      if (len >= 1) {
        Serial.print(" - Broker assigned ID: ");
        Serial.println(data[0], DEC);
      }
      break;
      
    default:
      Serial.println(" - Unknown message type");
      break;
  }
}

void handleSubscribe(uint8_t* data, int len) {
  if (len >= 3) {
    uint8_t clientId = data[0];
    uint16_t topicHash = (data[1] << 8) | data[2];
    
    Serial.print(" - Client ");
    Serial.print(clientId, DEC);
    Serial.print(" → Topic 0x");
    Serial.println(topicHash, HEX);
    
    trackTopic(topicHash);
  }
}

void handleUnsubscribe(uint8_t* data, int len) {
  if (len >= 3) {
    uint8_t clientId = data[0];
    uint16_t topicHash = (data[1] << 8) | data[2];
    
    Serial.print(" - Client ");
    Serial.print(clientId, DEC);
    Serial.print(" × Topic 0x");
    Serial.println(topicHash, HEX);
  }
}

void handlePublish(uint8_t* data, int len) {
  if (len >= 3) {
    uint8_t publisherId = data[0];
    uint16_t topicHash = (data[1] << 8) | data[2];
    
    String message = "";
    for (int i = 3; i < len; i++) {
      message += (char)data[i];
    }
    
    Serial.print(" - From ");
    Serial.print(publisherId, DEC);
    Serial.print(" on 0x");
    Serial.print(topicHash, HEX);
    Serial.print(": \"");
    Serial.print(message);
    Serial.println("\"");
    
    trackTopic(topicHash);
  }
}

void handleTopicData(uint8_t* data, int len) {
  if (len >= 3) {
    uint8_t targetId = data[0];
    uint16_t topicHash = (data[1] << 8) | data[2];
    
    String message = "";
    for (int i = 3; i < len; i++) {
      message += (char)data[i];
    }
    
    Serial.print(" - To ");
    Serial.print(targetId, DEC);
    Serial.print(" from 0x");
    Serial.print(topicHash, HEX);
    Serial.print(": \"");
    Serial.print(message);
    Serial.println("\"");
  }
}

void handleDirectMsg(uint8_t* data, int len) {
  if (len >= 1) {
    uint8_t senderId = data[0];
    
    String message = "";
    for (int i = 1; i < len; i++) {
      message += (char)data[i];
    }
    
    Serial.print(" - From ");
    Serial.print(senderId, DEC);
    Serial.print(": \"");
    Serial.print(message);
    Serial.println("\"");
  }
}

void handlePing(uint8_t* data, int len) {
  if (len >= 1) {
    Serial.print(" - From client ");
    Serial.println(data[0], DEC);
  }
}

void handlePong(uint8_t* data, int len) {
  if (len >= 2) {
    Serial.print(" - From broker to ");
    Serial.println(data[1], DEC);
  }
}

void handleAck(uint8_t* data, int len) {
  if (len >= 2) {
    Serial.print(" - From ");
    Serial.print(data[0], DEC);
    Serial.print(" to ");
    Serial.println(data[1], DEC);
  }
}

void trackTopic(uint16_t hash) {
  // Find existing topic
  for (uint8_t i = 0; i < topicCount; i++) {
    if (topics[i].hash == hash) {
      topics[i].count++;
      return;
    }
  }
  
  // Add new topic
  if (topicCount < 20) {
    topics[topicCount].hash = hash;
    topics[topicCount].count = 1;
    topics[topicCount].name = "0x" + String(hash, HEX);
    topicCount++;
  }
}

String getMessageTypeName(uint8_t type) {
  switch (type) {
    case 0x01: return "SUBSCRIBE   ";
    case 0x02: return "UNSUBSCRIBE ";
    case 0x03: return "PUBLISH     ";
    case 0x04: return "TOPIC_DATA  ";
    case 0x05: return "DIRECT_MSG  ";
    case 0x06: return "PING        ";
    case 0x07: return "PONG        ";
    case 0x08: return "ACK         ";
    case 0xFF: return "ID_REQUEST  ";
    case 0xFE: return "ID_RESPONSE ";
    default:   return "UNKNOWN     ";
  }
}

void printTime() {
  unsigned long ms = millis() - startTime;
  unsigned long seconds = ms / 1000;
  ms %= 1000;
  
  if (seconds < 10) Serial.print("0");
  Serial.print(seconds);
  Serial.print(".");
  if (ms < 100) Serial.print("0");
  if (ms < 10) Serial.print("0");
  Serial.print(ms);
}

void printStatistics() {
  unsigned long uptime = (millis() - startTime) / 1000;
  
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════╗");
  Serial.println("║         Network Statistics                ║");
  Serial.println("╚═══════════════════════════════════════════╝");
  Serial.println();
  Serial.print("Uptime:          "); Serial.print(uptime); Serial.println(" seconds");
  Serial.print("Total messages:  "); Serial.println(totalMessages);
  Serial.print("Msg/sec:         "); Serial.println(totalMessages / (uptime + 1));
  Serial.println();
  Serial.println("Message Types:");
  Serial.print("  Subscribes:    "); Serial.println(subscribeCount);
  Serial.print("  Unsubscribes:  "); Serial.println(unsubscribeCount);
  Serial.print("  Publishes:     "); Serial.println(publishCount);
  Serial.print("  Direct msgs:   "); Serial.println(directMsgCount);
  Serial.print("  Pings:         "); Serial.println(pingCount);
  Serial.print("  ID requests:   "); Serial.println(idRequestCount);
  Serial.println();
  Serial.print("Active topics:   "); Serial.println(topicCount);
  Serial.println("─────────────────────────────────────────────");
  Serial.println();
}

void printTopics() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════╗");
  Serial.println("║         Active Topics                     ║");
  Serial.println("╚═══════════════════════════════════════════╝");
  Serial.println();
  
  if (topicCount == 0) {
    Serial.println("No topics tracked yet.");
  } else {
    Serial.println("Hash     Count  Name");
    Serial.println("────────────────────────────");
    for (uint8_t i = 0; i < topicCount; i++) {
      Serial.print("0x");
      if (topics[i].hash < 0x1000) Serial.print("0");
      if (topics[i].hash < 0x100) Serial.print("0");
      if (topics[i].hash < 0x10) Serial.print("0");
      Serial.print(topics[i].hash, HEX);
      Serial.print("   ");
      Serial.print(topics[i].count);
      Serial.print("     ");
      Serial.println(topics[i].name);
    }
  }
  
  Serial.println();
  Serial.println("─────────────────────────────────────────────");
  Serial.println();
}

void resetStatistics() {
  totalMessages = 0;
  subscribeCount = 0;
  unsubscribeCount = 0;
  publishCount = 0;
  directMsgCount = 0;
  pingCount = 0;
  idRequestCount = 0;
  topicCount = 0;
  startTime = millis();
  
  Serial.println();
  Serial.println("✓ Statistics reset");
  Serial.println();
}

void printHelp() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════╗");
  Serial.println("║         Monitor Commands                  ║");
  Serial.println("╚═══════════════════════════════════════════╝");
  Serial.println();
  Serial.println("  s - Show statistics");
  Serial.println("  t - Show active topics");
  Serial.println("  r - Reset statistics");
  Serial.println("  h - Show this help");
  Serial.println();
  Serial.println("─────────────────────────────────────────────");
  Serial.println();
}
