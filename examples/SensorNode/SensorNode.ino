/*
  CAN PubSub Sensor Node Example
  
  This example demonstrates a sensor node that publishes data to topics
  and responds to commands from the broker or other nodes.
  
  Features:
  - Publishes sensor data periodically
  - Subscribes to control topics
  - Responds to direct messages
  - Automatic reconnection
  
  Circuit:
  - MCP2515 CAN module connected to SPI pins
  - Temperature sensor on A0 (optional)
  - LED on pin 13
  
  Topics:
  - sensors/temperature - Published every 5 seconds
  - sensors/status      - Published on status change
  - control/led         - Subscribe for LED control
  - control/interval    - Subscribe for interval changes
  
  Created 2025
  by Juan Pablo Risso
*/

#include <SUPER_CAN.h>

// Create client instance
CANPubSubClient client(CAN);

// Configuration
const int LED_PIN = 13;
const int TEMP_SENSOR_PIN = A0;
unsigned long publishInterval = 5000;  // 5 seconds
unsigned long lastPublish = 0;
bool ledState = false;

// Sensor data
float temperature = 0.0;
int sensorValue = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  // Setup hardware
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("=== CAN PubSub Sensor Node ===");
  Serial.println("Initializing...");

  // Initialize CAN bus
  if (!CAN.begin(500E3)) {
    Serial.println("Starting CAN failed!");
    while (1);
  }
  
  // Setup callbacks
  client.onConnect(onConnect);
  client.onMessage(onMessage);
  client.onDirectMessage(onDirectMessage);
  
  // Connect to broker
  connectToBroker();
  
  Serial.println("Sensor node ready!");
}

void loop() {
  // Check connection
  if (!client.isConnected()) {
    Serial.println("Connection lost! Reconnecting...");
    connectToBroker();
    delay(1000);
    return;
  }
  
  // Process incoming messages
  client.loop();
  
  // Publish sensor data periodically
  if (millis() - lastPublish >= publishInterval) {
    readSensors();
    publishSensorData();
    lastPublish = millis();
  }
}

void connectToBroker() {
  Serial.println("Connecting to broker...");
  
  if (client.begin(5000)) {
    Serial.print("Connected! Client ID: 0x");
    Serial.println(client.getClientId(), HEX);
    
    // Subscribe to control topics
    client.subscribe("control/led");
    client.subscribe("control/interval");
    
    Serial.println("Subscribed to control topics");
    
    // Publish initial status
    client.publish("sensors/status", "online");
  } else {
    Serial.println("Connection failed!");
  }
}

void readSensors() {
  // Read analog sensor (simulated temperature)
  sensorValue = analogRead(TEMP_SENSOR_PIN);
  
  // Convert to temperature (example calculation)
  // Adjust this based on your actual sensor
  temperature = (sensorValue * 5.0 / 1023.0) * 100.0;
}

void publishSensorData() {
  // Publish temperature
  String tempStr = String(temperature, 2);
  if (client.publish("sensors/temperature", tempStr)) {
    Serial.print("Published temperature: ");
    Serial.print(tempStr);
    Serial.println(" °C");
  }
  
  // Publish raw sensor value
  String sensorStr = String(sensorValue);
  client.publish("sensors/raw", sensorStr);
}

void onConnect() {
  Serial.println("Connection established!");
}

void onMessage(uint16_t topicHash, const String& topic, const String& message) {
  Serial.print("Received on [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  // Handle control messages
  if (topic == "control/led") {
    handleLedControl(message);
  } else if (topic == "control/interval") {
    handleIntervalControl(message);
  }
}

void handleLedControl(const String& message) {
  if (message == "on" || message == "1") {
    digitalWrite(LED_PIN, HIGH);
    ledState = true;
    Serial.println("LED turned ON");
    client.publish("sensors/status", "LED:ON");
  } else if (message == "off" || message == "0") {
    digitalWrite(LED_PIN, LOW);
    ledState = false;
    Serial.println("LED turned OFF");
    client.publish("sensors/status", "LED:OFF");
  } else if (message == "toggle") {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    Serial.print("LED toggled to ");
    Serial.println(ledState ? "ON" : "OFF");
    client.publish("sensors/status", ledState ? "LED:ON" : "LED:OFF");
  }
}

void handleIntervalControl(const String& message) {
  long newInterval = message.toInt();
  if (newInterval >= 1000 && newInterval <= 60000) {
    publishInterval = newInterval;
    Serial.print("Publish interval changed to ");
    Serial.print(publishInterval);
    Serial.println(" ms");
    
    String statusMsg = "Interval:" + String(publishInterval);
    client.publish("sensors/status", statusMsg);
  } else {
    Serial.println("Invalid interval (must be 1000-60000 ms)");
  }
}

void onDirectMessage(uint8_t senderId, const String& message) {
  Serial.print("Direct message from 0x");
  Serial.print(senderId, HEX);
  Serial.print(": ");
  Serial.println(message);
  
  // Handle direct commands
  if (message == "status") {
    String response = "Temp:" + String(temperature, 2) + 
                     " LED:" + String(ledState ? "ON" : "OFF") +
                     " Interval:" + String(publishInterval);
    client.sendDirectMessage(response);
  } else if (message == "ping") {
    client.sendDirectMessage("pong");
  }
}
