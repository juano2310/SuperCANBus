// ESP32-C3 CAN Example with MCP2515
// ESP32-C3 doesn't have a built-in CAN controller, so we use an external MCP2515 via SPI
// 
// Hardware connections:
// MCP2515 Module -> ESP32-C3
// VCC    -> 3.3V or 5V (check your module)
// GND    -> GND
// CS     -> GPIO 7 (configurable)
// SO     -> GPIO 2 (MISO)
// SI     -> GPIO 3 (MOSI)
// SCK    -> GPIO 4 (SCK)
// INT    -> GPIO 8 (configurable, optional)

#include <SuperCANBus.h>
#include <SPI.h>

// Pin definitions for ESP32-C3
#define CAN_CS_PIN    7   // Chip Select
#define CAN_INT_PIN   8   // Interrupt (optional, use -1 if not connected)

// Create MCP2515 CAN controller instance
MCP2515Class CAN;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("ESP32-C3 CAN with MCP2515 Example");

  // Initialize SPI (ESP32-C3 default SPI pins: MISO=2, MOSI=3, SCK=4)
  SPI.begin();

  // Set the CS and INT pins for the MCP2515
  CAN.setPins(CAN_CS_PIN, CAN_INT_PIN);
  
  // Start CAN bus at 500 kbps
  if (!CAN.begin(500E3)) {
    Serial.println("Starting CAN failed!");
    while (1);
  }
  
  Serial.println("CAN initialized successfully!");
  Serial.println("Listening for CAN messages...");
}

void loop() {
  // Try to parse a CAN packet
  int packetSize = CAN.parsePacket();

  if (packetSize) {
    // Received a packet
    Serial.print("Received ");

    if (CAN.packetExtended()) {
      Serial.print("extended ");
    }

    if (CAN.packetRtr()) {
      // Remote transmission request, packet contains no data
      Serial.print("RTR ");
    }

    Serial.print("packet with id 0x");
    Serial.print(CAN.packetId(), HEX);

    if (CAN.packetRtr()) {
      Serial.print(" and requested length ");
      Serial.println(CAN.packetDlc());
    } else {
      Serial.print(" and length ");
      Serial.println(packetSize);

      // Print packet data
      Serial.print("Data: ");
      while (CAN.available()) {
        Serial.print((char)CAN.read());
      }
      Serial.println();
    }

    Serial.println();
  }

  // Send a packet every 2 seconds
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 2000) {
    lastSend = millis();
    
    // Send packet with ID 0x123
    CAN.beginPacket(0x123);
    CAN.write('H');
    CAN.write('e');
    CAN.write('l');
    CAN.write('l');
    CAN.write('o');
    CAN.endPacket();
    
    Serial.println("Sent: Hello (ID: 0x123)");
  }
}
