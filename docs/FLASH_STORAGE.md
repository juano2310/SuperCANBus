# Flash Storage Quick Reference

## Overview
Client ID to serial number mappings are **automatically stored in flash memory** and persist across power cycles.

## Platform Support

| Platform | Storage Type | Library | Capacity |
|----------|-------------|---------|----------|
| ESP32 | NVS (Preferences) | `Preferences.h` | ~100,000 writes/sector |
| Arduino AVR | EEPROM | `EEPROM.h` | ~100,000 writes |
| ESP8266 | EEPROM emulation | `EEPROM.h` | ~10,000 writes |
| Arduino SAMD | Flash | `FlashStorage.h` | ~10,000 writes |

## Automatic Storage

Storage operations happen **automatically**:

```cpp
broker.begin();                    // ← Loads from flash automatically
broker.registerClient("ESP32_001"); // ← Saves to flash automatically
broker.unregisterClient(0x10);     // ← Saves to flash automatically
```

## Manual Storage Control

For advanced use cases:

```cpp
// Manual save (automatic saves disabled)
broker.saveMappingsToStorage();

// Manual load (useful after external changes)
broker.loadMappingsFromStorage();

// Clear all stored data
broker.clearStoredMappings();
```

## Storage Size

```
Default configuration:
- Maximum clients: 50
- Serial length: 32 chars
- Storage per client: 34 bytes
- Total storage: ~1700 bytes

Total memory usage:
- Header: 4 bytes
- Client data: 34 × 50 = 1700 bytes
- Total: 1704 bytes
```

## Configuration

Edit `CANMqtt.h` to customize:

```cpp
#define MAX_CLIENT_MAPPINGS 50    // More clients = more storage
#define MAX_SERIAL_LENGTH 32      // Longer serials = more storage
#define EEPROM_SIZE 2048          // Total EEPROM size (Arduino)
```

## Power Cycle Testing

```cpp
// Step 1: Register clients
broker.registerClient("TEST_001");
broker.registerClient("TEST_002");

// Step 2: Check count
Serial.println(broker.getRegisteredClientCount()); // → 2

// Step 3: Power off device

// Step 4: Power on and check
broker.begin();
Serial.println(broker.getRegisteredClientCount()); // → 2 (persisted!)
```

## Storage Verification

```cpp
void setup() {
  broker.begin();
  
  // Check if data was loaded
  uint8_t count = broker.getRegisteredClientCount();
  if (count > 0) {
    Serial.print("Loaded ");
    Serial.print(count);
    Serial.println(" clients from flash");
  } else {
    Serial.println("Fresh start (no stored data)");
  }
}
```

## Data Format

```
Flash Memory Layout:
┌─────────────────────────────────────┐
│ Magic Number (0xCABE)     [2 bytes] │ ← Validates data
├─────────────────────────────────────┤
│ Mapping Count              [1 byte]  │
├─────────────────────────────────────┤
│ Next Client ID             [1 byte]  │
├─────────────────────────────────────┤
│ Client Mapping #1         [34 bytes] │
│   - Client ID              [1 byte]  │
│   - Serial Number         [32 bytes] │
│   - Active Flag            [1 byte]  │
├─────────────────────────────────────┤
│ Client Mapping #2         [34 bytes] │
├─────────────────────────────────────┤
│ ...                                  │
├─────────────────────────────────────┤
│ Client Mapping #50        [34 bytes] │
└─────────────────────────────────────┘
```

## Error Handling

```cpp
// Check if load was successful
if (!broker.loadMappingsFromStorage()) {
  Serial.println("No valid data in storage");
  // This is normal for first run
}

// Check if save was successful
if (!broker.saveMappingsToStorage()) {
  Serial.println("Save failed!");
  // Rare - indicates storage hardware issue
}

// Verify magic number
if (loadFailed) {
  // Either:
  // 1. First run (no data yet)
  // 2. Flash corruption
  // 3. Different version
  broker.clearStoredMappings(); // Start fresh
}
```

## Best Practices

### ✅ DO:
- Let the library handle storage automatically
- Check `getRegisteredClientCount()` after `begin()`
- Use serial numbers < 32 characters
- Test power cycle behavior
- Periodically verify storage integrity

### ❌ DON'T:
- Manually write to EEPROM addresses used by library
- Call `saveMappingsToStorage()` excessively (wear)
- Store >50 clients without increasing `MAX_CLIENT_MAPPINGS`
- Use random serial numbers (defeats persistence)
- Ignore load/save return values in production

## Wear Management

Flash/EEPROM has limited write cycles. The library minimizes writes:

```cpp
// Each of these triggers ONE write:
broker.registerClient("NEW_001");      // New registration
broker.unregisterClient(0x10);         // State change
broker.updateClientSerial(0x10, "X");  // Update

// These do NOT trigger writes:
broker.getClientIdBySerial("ESP32");   // Read only
broker.getSerialByClientId(0x10);      // Read only
broker.listRegisteredClients(...);     // Read only
```

**Estimated lifetime:**
- 50 clients registering/unregistering daily
- 1 write per operation
- 50 writes/day × 365 days = 18,250 writes/year
- ESP32: 100,000 writes = **5+ years**
- Arduino AVR: 100,000 writes = **5+ years**
- ESP8266: 10,000 writes = **6 months** (use carefully)

## Troubleshooting

### Mappings not persisting?

```cpp
// Check platform
#ifdef ESP32
  Serial.println("Using ESP32 Preferences");
#else
  Serial.println("Using EEPROM");
#endif

// Verify save
if (broker.saveMappingsToStorage()) {
  Serial.println("Save OK");
} else {
  Serial.println("Save FAILED");
}

// Check after reboot
broker.begin();
Serial.print("Loaded: ");
Serial.println(broker.getRegisteredClientCount());
```

### Storage full?

```cpp
uint8_t count = broker.getRegisteredClientCount();
if (count >= MAX_CLIENT_MAPPINGS) {
  Serial.println("Storage full! Increase MAX_CLIENT_MAPPINGS");
}
```

### Corruption?

```cpp
// Reset to factory defaults
broker.clearStoredMappings();
ESP.restart(); // or reset device
```

## Example: Storage Test

See `examples/CANMqttStorageTest/` for a complete storage verification sketch.

```cpp
// Register test clients
broker.registerClient("TEST_001");
broker.registerClient("TEST_002");

// Power cycle device...

// After reboot
broker.begin();
Serial.print("Loaded: ");
Serial.println(broker.getRegisteredClientCount()); // → 2

// List them
broker.listRegisteredClients([](uint8_t id, const String& sn, bool active) {
  Serial.print("ID: 0x");
  Serial.print(id, HEX);
  Serial.print(" → ");
  Serial.println(sn);
});
```

---

**Key Point**: Flash storage makes your CAN network configuration **persistent and reliable**, eliminating manual reconfiguration after power loss!
