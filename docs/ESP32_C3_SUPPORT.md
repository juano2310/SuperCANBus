# ESP32-C3 and External CAN Controller Support

## Important Information for ESP32-C3/C6/H2 Users

The **ESP32-C3**, **ESP32-C6**, **ESP32-H2**, and **ESP32-C2** variants **do not have a built-in CAN controller**. Only the original **ESP32** (ESP32-WROOM, ESP32-WROVER) has the integrated SJA1000 CAN controller.

### Hardware Requirements for ESP32-C3/C6/H2/C2

To use CAN bus with these boards, you need an **external CAN controller module** such as:
- **MCP2515** (most common, connects via SPI)
- **SPI-based CAN modules**

### Wiring Example for MCP2515 with ESP32-C3

```
MCP2515 Module -> ESP32-C3
VCC    -> 3.3V or 5V (check your module voltage)
GND    -> GND
CS     -> GPIO 7 (configurable)
SO     -> GPIO 2 (MISO - default SPI)
SI     -> GPIO 3 (MOSI - default SPI)
SCK    -> GPIO 4 (SCK - default SPI)
INT    -> GPIO 8 (optional interrupt pin)
```

### Code Example

```cpp
#include <SUPER_CAN.h>
#include <SPI.h>

#define CAN_CS_PIN    7
#define CAN_INT_PIN   8

// Create MCP2515 instance (not using the built-in CAN object)
MCP2515Class CAN;

void setup() {
  Serial.begin(115200);
  
  // Initialize SPI
  SPI.begin();
  
  // Set CS and INT pins
  CAN.setPins(CAN_CS_PIN, CAN_INT_PIN);
  
  // Start CAN at 500 kbps
  if (!CAN.begin(500E3)) {
    Serial.println("CAN init failed!");
    while (1);
  }
  
  Serial.println("CAN ready!");
}

void loop() {
  int packetSize = CAN.parsePacket();
  
  if (packetSize) {
    Serial.print("Received packet with ID 0x");
    Serial.println(CAN.packetId(), HEX);
  }
}
```

### Supported Boards

| Board | Built-in CAN | External CAN Required |
|-------|--------------|----------------------|
| ESP32 (original) | ✅ Yes (SJA1000) | Optional |
| ESP32-S2 | ✅ Yes (TWAI) | Optional |
| ESP32-S3 | ✅ Yes (TWAI) | Optional |
| ESP32-C3 | ❌ No | Required (MCP2515) |
| ESP32-C6 | ❌ No | Required (MCP2515) |
| ESP32-H2 | ❌ No | Required (MCP2515) |
| ESP32-C2 | ❌ No | Required (MCP2515) |

### See Also

- Example: `examples/ESP32_C3_MCP2515/`
- MCP2515 documentation
