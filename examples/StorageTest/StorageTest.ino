/*
  CAN MQTT Storage Test
  
  This example tests the flash memory storage feature for client mappings.
  It demonstrates how client registrations survive power cycles.
  
  Test Procedure:
  1. Upload and run this sketch
  2. Register some clients (they will be stored in flash)
  3. Power off the device
  4. Power on the device
  5. Check that the same clients are still registered
  
  Circuit:
  - MCP2515 CAN module connected to SPI pins
  - Or ESP32 with built-in CAN controller
  
  Created 2025
  by Juan Pablo Risso
*/

#include <SUPER_CAN.h>

CANMqttBroker broker(CAN);

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n╔═══════════════════════════════════════════════╗");
  Serial.println("║     CAN MQTT Flash Storage Test              ║");
  Serial.println("╚═══════════════════════════════════════════════╝\n");
  
  // Initialize CAN
  Serial.print("Initializing CAN bus... ");
  if (!CAN.begin(500E3)) {
    Serial.println("FAILED!");
    while (1);
  }
  Serial.println("OK");
  
  // Initialize broker (this automatically loads from storage)
  Serial.print("Starting broker... ");
  if (!broker.begin()) {
    Serial.println("FAILED!");
    while (1);
  }
  Serial.println("OK");
  
  // Check how many clients were loaded
  uint8_t count = broker.getRegisteredClientCount();
  Serial.print("\n✓ Loaded ");
  Serial.print(count);
  Serial.println(" client(s) from flash memory\n");
  
  if (count > 0) {
    Serial.println("Previously registered clients:");
    Serial.println("─────────────────────────────────────────────────");
    broker.listRegisteredClients([](uint8_t id, const String& serial, bool active) {
      Serial.print("  ID: 0x");
      if (id < 0x10) Serial.print("0");
      Serial.print(id, HEX);
      Serial.print("  SN: ");
      Serial.print(serial);
      Serial.print("  [");
      Serial.print(active ? "Active" : "Inactive");
      Serial.println("]");
    });
    Serial.println("─────────────────────────────────────────────────\n");
  } else {
    Serial.println("No previous registrations found (fresh start)\n");
  }
  
  // Test menu
  Serial.println("Commands:");
  Serial.println("  1 - Register test client 'TEST_001'");
  Serial.println("  2 - Register test client 'TEST_002'");
  Serial.println("  3 - Register test client 'TEST_003'");
  Serial.println("  l - List all registered clients");
  Serial.println("  c - Clear all stored mappings");
  Serial.println("  r - Reload from storage");
  Serial.println("  s - Save to storage manually");
  Serial.println("\n⚡ After registering clients, power cycle to test!\n");
}

void loop() {
  broker.loop();
  
  if (Serial.available()) {
    char cmd = Serial.read();
    
    switch (cmd) {
      case '1':
        registerTestClient("TEST_001");
        break;
        
      case '2':
        registerTestClient("TEST_002");
        break;
        
      case '3':
        registerTestClient("TEST_003");
        break;
        
      case 'l':
      case 'L':
        listClients();
        break;
        
      case 'c':
      case 'C':
        clearStorage();
        break;
        
      case 'r':
      case 'R':
        reloadFromStorage();
        break;
        
      case 's':
      case 'S':
        saveToStorage();
        break;
    }
  }
}

void registerTestClient(const char* serialNumber) {
  Serial.print("\nRegistering client: ");
  Serial.print(serialNumber);
  Serial.print(" ... ");
  
  uint8_t id = broker.registerClient(serialNumber);
  
  if (id != CAN_MQTT_UNASSIGNED_ID) {
    Serial.print("OK - Assigned ID: 0x");
    if (id < 0x10) Serial.print("0");
    Serial.println(id, HEX);
    Serial.println("✓ Saved to flash memory");
  } else {
    Serial.println("FAILED (table full)");
  }
}

void listClients() {
  uint8_t count = broker.getRegisteredClientCount();
  
  Serial.print("\n📋 Registered Clients (");
  Serial.print(count);
  Serial.println("):");
  Serial.println("─────────────────────────────────────────────────");
  
  if (count == 0) {
    Serial.println("  (none)");
  } else {
    broker.listRegisteredClients([](uint8_t id, const String& serial, bool active) {
      Serial.print("  ID: 0x");
      if (id < 0x10) Serial.print("0");
      Serial.print(id, HEX);
      Serial.print("  SN: ");
      
      // Pad serial number for alignment
      Serial.print(serial);
      for (int i = serial.length(); i < 12; i++) {
        Serial.print(" ");
      }
      
      Serial.print("  [");
      Serial.print(active ? "Active  " : "Inactive");
      Serial.println("]");
    });
  }
  
  Serial.println("─────────────────────────────────────────────────\n");
}

void clearStorage() {
  Serial.println("\n⚠ WARNING: Clearing all stored mappings!");
  Serial.print("Are you sure? (y/n): ");
  
  unsigned long start = millis();
  while (!Serial.available() && (millis() - start) < 5000);
  
  if (Serial.available()) {
    char confirm = Serial.read();
    Serial.println(confirm);
    
    if (confirm == 'y' || confirm == 'Y') {
      if (broker.clearStoredMappings()) {
        Serial.println("✓ All mappings cleared from flash");
        Serial.println("⚡ Power cycle to verify!\n");
      } else {
        Serial.println("✗ Failed to clear storage\n");
      }
    } else {
      Serial.println("✗ Cancelled\n");
    }
  } else {
    Serial.println("Timeout - cancelled\n");
  }
}

void reloadFromStorage() {
  Serial.println("\n🔄 Reloading from flash memory...");
  
  if (broker.loadMappingsFromStorage()) {
    Serial.print("✓ Loaded ");
    Serial.print(broker.getRegisteredClientCount());
    Serial.println(" client(s)");
    listClients();
  } else {
    Serial.println("✗ No valid data in storage\n");
  }
}

void saveToStorage() {
  Serial.print("\n💾 Saving to flash memory... ");
  
  if (broker.saveMappingsToStorage()) {
    Serial.println("OK");
    Serial.print("✓ Saved ");
    Serial.print(broker.getRegisteredClientCount());
    Serial.println(" client(s) to flash\n");
  } else {
    Serial.println("FAILED\n");
  }
}
