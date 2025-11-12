/*
  Subscription Restoration Demo
  
  This example demonstrates automatic subscription restoration when a client
  reconnects with the same serial number. Watch as the client maintains its
  subscriptions across power cycles without any manual re-subscription!
  
  How to test:
  1. Upload this sketch to a client node with serial number enabled
  2. Open serial monitor - you'll see the client subscribe to topics
  3. Reset the Arduino (or power cycle it)
  4. Watch as the subscriptions are automatically restored!
  5. No need to call subscribe() again - broker remembers everything!
  
  What you'll see:
  - First boot: Client subscribes to topics
  - After reset: ">>> Subscription restored: [topic_name]" messages
  - Client immediately ready to receive messages
  
  Requirements:
  - Broker with serial number management (BrokerWithSerial.ino)
  - Client must use persistent serial number
  
  Circuit:
  - MCP2515 CAN module connected to SPI pins
  - Or ESP32 with built-in CAN controller
  
  Created 2025
  by Juan Pablo Risso
*/

#include <SuperCANBus.h>

CANPubSubClient client(CAN);

// Use unique serial number (ESP32 chip ID or custom)
String SERIAL_NUMBER;

// Track first boot vs reconnection
bool isFirstBoot = true;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║    Subscription Restoration Demo              ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  
  // Generate unique serial number
  #ifdef ESP32
    uint64_t chipid = ESP.getEfuseMac();
    SERIAL_NUMBER = "ESP32_" + String((uint32_t)(chipid >> 32), HEX) + 
                                String((uint32_t)chipid, HEX);
    SERIAL_NUMBER.toUpperCase();
  #else
    SERIAL_NUMBER = "DEMO_" + String(random(1000, 9999));
  #endif
  
  Serial.print("Serial Number: ");
  Serial.println(SERIAL_NUMBER);
  Serial.println();
  
  // Initialize CAN bus
  Serial.println("Initializing CAN bus...");
  if (!CAN.begin(500E3)) {
    Serial.println("ERROR: Starting CAN failed!");
    while (1);
  }
  Serial.println("✓ CAN bus initialized");
  
  // Setup callbacks
  client.onConnect(onConnect);
  client.onMessage(onMessage);
  
  // Connect to broker with serial number
  Serial.println("Connecting to broker...");
  if (client.begin(SERIAL_NUMBER, 5000)) {
    Serial.print("✓ Connected! Client ID: ");
    Serial.println(client.getClientId(), DEC);
    Serial.println();
    
    // Check if we have subscriptions (would be restored)
    uint8_t subCount = client.getSubscriptionCount();
    
    if (subCount > 0) {
      // We have subscriptions - this is a reconnection!
      isFirstBoot = false;
      Serial.println("╔═══════════════════════════════════════════════╗");
      Serial.println("║    SUBSCRIPTION RESTORATION SUCCESSFUL!       ║");
      Serial.println("╚═══════════════════════════════════════════════╝");
      Serial.println();
      Serial.print("Restored ");
      Serial.print(subCount);
      Serial.println(" subscription(s) from broker:");
      Serial.println();
      
      // List restored subscriptions
      client.listSubscribedTopics([](uint16_t hash, const String& name) {
        Serial.print("  ✓ ");
        Serial.print(name);
        Serial.print(" (0x");
        Serial.print(hash, HEX);
        Serial.println(")");
      });
      
      Serial.println();
      Serial.println("Client is ready to receive messages!");
      Serial.println("No manual re-subscription needed! 🚀");
      Serial.println();
    } else {
      // First boot - subscribe to topics
      isFirstBoot = true;
      Serial.println("First connection detected.");
      Serial.println("Subscribing to demo topics...");
      Serial.println();
      
      // Subscribe to several topics
      if (client.subscribe("sensors/temperature")) {
        Serial.println("✓ Subscribed to: sensors/temperature");
      }
      
      if (client.subscribe("sensors/humidity")) {
        Serial.println("✓ Subscribed to: sensors/humidity");
      }
      
      if (client.subscribe("status/system")) {
        Serial.println("✓ Subscribed to: status/system");
      }
      
      if (client.subscribe("alerts/critical")) {
        Serial.println("✓ Subscribed to: alerts/critical");
      }
      
      Serial.println();
      Serial.println("╔═══════════════════════════════════════════════╗");
      Serial.println("║  Subscriptions saved to broker's flash memory ║");
      Serial.println("╚═══════════════════════════════════════════════╝");
      Serial.println();
      Serial.println("📝 To test restoration:");
      Serial.println("   1. Press the RESET button on your board");
      Serial.println("   2. Watch as subscriptions are restored automatically!");
      Serial.println("   3. No code changes needed - it just works!");
      Serial.println();
    }
  } else {
    Serial.println("✗ Failed to connect to broker!");
    Serial.println("  Make sure BrokerWithSerial.ino is running.");
  }
  
  Serial.println("────────────────────────────────────────────────────");
  Serial.println("Listening for messages...");
  Serial.println();
}

void loop() {
  client.loop();
  
  // Publish test messages periodically (every 10 seconds)
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish >= 10000) {
    if (client.isConnected()) {
      // Publish to topics we're subscribed to (will receive back)
      String tempValue = String(20 + random(0, 15));
      client.publish("sensors/temperature", tempValue + "°C");
      
      String humValue = String(40 + random(0, 30));
      client.publish("sensors/humidity", humValue + "%");
      
      Serial.println("Published test data to demonstrate message reception");
    }
    lastPublish = millis();
  }
  
  // Handle serial commands
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input == "status") {
      showStatus();
    } else if (input == "reset") {
      Serial.println("Reset the board to see subscription restoration!");
    } else if (input == "help") {
      Serial.println("Commands:");
      Serial.println("  status - Show connection and subscription status");
      Serial.println("  reset  - Instructions for testing restoration");
      Serial.println("  help   - Show this help");
    }
  }
}

void onConnect() {
  Serial.println(">>> Connected to broker");
}

void onMessage(uint16_t topicHash, const String& topic, const String& message) {
  Serial.print("📨 [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
}

void showStatus() {
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════╗");
  Serial.println("║         Status                                 ║");
  Serial.println("╚═══════════════════════════════════════════════╝");
  Serial.println();
  Serial.print("Connected:       ");
  Serial.println(client.isConnected() ? "Yes" : "No");
  Serial.print("Client ID:       ");
  Serial.println(client.getClientId(), DEC);
  Serial.print("Serial Number:   ");
  Serial.println(SERIAL_NUMBER);
  Serial.print("Subscriptions:   ");
  Serial.println(client.getSubscriptionCount());
  Serial.print("First Boot:      ");
  Serial.println(isFirstBoot ? "Yes" : "No (Restored)");
  Serial.println();
  
  if (client.getSubscriptionCount() > 0) {
    Serial.println("Active subscriptions:");
    client.listSubscribedTopics([](uint16_t hash, const String& name) {
      Serial.print("  • ");
      Serial.println(name);
    });
    Serial.println();
  }
}
