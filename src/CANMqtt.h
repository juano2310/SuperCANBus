// CANMqtt.h - Publish/Subscribe protocol for CAN bus
// Part of Super CAN+ Library

#ifndef CAN_MQTT_H
#define CAN_MQTT_H

#include <Arduino.h>
#include <functional>
#include "CANController.h"

// Platform-specific storage includes
#ifdef ESP32
  #include <Preferences.h>
#else
  #include <EEPROM.h>
#endif

// Message types
#define CAN_MQTT_SUBSCRIBE      0x01
#define CAN_MQTT_UNSUBSCRIBE    0x02
#define CAN_MQTT_PUBLISH        0x03
#define CAN_MQTT_TOPIC_DATA     0x04
#define CAN_MQTT_DIRECT_MSG     0x05
#define CAN_MQTT_ID_REQUEST     0xFF
#define CAN_MQTT_ID_RESPONSE    0xFE
#define CAN_MQTT_PING           0x06
#define CAN_MQTT_PONG           0x07
#define CAN_MQTT_ACK            0x08

#define CAN_MQTT_BROKER_ID      0x00
#define CAN_MQTT_UNASSIGNED_ID  0xFF

#define MAX_SUBSCRIPTIONS       20
#define MAX_SUBSCRIBERS_PER_TOPIC 10
#define MAX_CLIENT_TOPICS       10
#define MAX_MESSAGE_CALLBACKS   5
#define MAX_CLIENT_MAPPINGS     50  // Maximum number of registered clients
#define MAX_SERIAL_LENGTH       32  // Maximum length for serial numbers

// Forward declarations
class CANMqttBroker;
class CANMqttClient;

// Callback types
typedef void (*MessageCallback)(uint16_t topicHash, const String& topic, const String& message);
typedef void (*DirectMessageCallback)(uint8_t senderId, const String& message);
typedef void (*ConnectionCallback)(uint8_t clientId);

// Subscription structure for broker
struct Subscription {
  uint16_t topicHash;
  uint8_t subscribers[MAX_SUBSCRIBERS_PER_TOPIC];
  uint8_t subCount;
};

// Topic mapping structure
struct TopicMapping {
  uint16_t hash;
  String name;
};

// Client ID mapping structure (ID <-> Serial Number)
// This structure is stored in flash memory
struct ClientMapping {
  uint8_t clientId;
  char serialNumber[MAX_SERIAL_LENGTH];
  bool active;
  
  // Helper methods for String compatibility
  void setSerial(const String& serial) {
    strncpy(serialNumber, serial.c_str(), MAX_SERIAL_LENGTH - 1);
    serialNumber[MAX_SERIAL_LENGTH - 1] = '\0';
  }
  
  String getSerial() const {
    return String(serialNumber);
  }
};

// Storage configuration
#define STORAGE_NAMESPACE "canmqtt"
#define STORAGE_MAGIC 0xCABE     // Magic number to verify valid data
#define EEPROM_SIZE 2048         // EEPROM size for non-ESP32 platforms

// Base pub/sub class
class CANMqttBase {
public:
  CANMqttBase(CANControllerClass& can);
  
  // Topic hashing
  static uint16_t hashTopic(const String& topic);
  
  // Topic name management
  void registerTopic(const String& topic);
  String getTopicName(uint16_t hash);
  
protected:
  CANControllerClass* _can;
  TopicMapping _topicMappings[MAX_SUBSCRIPTIONS];
  uint8_t _topicMappingCount;
};

// Pub/Sub Broker class
class CANMqttBroker : public CANMqttBase {
public:
  CANMqttBroker(CANControllerClass& can);
  
  // Initialization
  bool begin();
  void end();
  
  // Main loop processing
  void loop();
  
  // Message handling
  void handleMessage(int packetSize);
  
  // Callbacks
  void onClientConnect(ConnectionCallback callback);
  void onClientDisconnect(ConnectionCallback callback);
  void onPublish(MessageCallback callback);
  void onDirectMessage(DirectMessageCallback callback);
  
  // Broker operations
  void sendToClient(uint8_t clientId, uint16_t topicHash, const String& message);
  void sendDirectMessage(uint8_t clientId, const String& message);
  void broadcastMessage(uint16_t topicHash, const String& message);
  
  // Statistics
  uint8_t getClientCount();
  uint8_t getSubscriptionCount();
  void getSubscribers(uint16_t topicHash, uint8_t* subscribers, uint8_t* count);
  void listSubscribedTopics(std::function<void(uint16_t hash, const String& name, uint8_t subscriberCount)> callback);
  
  // Client ID mapping management
  uint8_t registerClient(const String& serialNumber);
  bool unregisterClient(uint8_t clientId);
  bool unregisterClientBySerial(const String& serialNumber);
  uint8_t getClientIdBySerial(const String& serialNumber);
  String getSerialByClientId(uint8_t clientId);
  bool updateClientSerial(uint8_t clientId, const String& newSerial);
  uint8_t getRegisteredClientCount();
  void listRegisteredClients(std::function<void(uint8_t id, const String& serial, bool active)> callback);
  
  // Persistent storage management
  bool loadMappingsFromStorage();
  bool saveMappingsToStorage();
  bool clearStoredMappings();
  
private:
  // Subscription management
  void addSubscription(uint8_t clientId, uint16_t topicHash);
  void removeSubscription(uint8_t clientId, uint16_t topicHash);
  void removeAllSubscriptions(uint8_t clientId);
  void forwardToSubscribers(uint16_t topicHash, const String& message);
  
  // Client ID management (old method for backward compatibility)
  void assignClientID();
  
  // Client ID management with serial number
  void handleIdRequestWithSerial();
  uint8_t findOrCreateClientId(const String& serialNumber);
  int findClientMapping(const String& serialNumber);
  int findClientMappingById(uint8_t clientId);
  
  // Message handlers
  void handleSubscribe();
  void handleUnsubscribe();
  void handlePublish();
  void handleDirectMessage();
  void handlePing();
  
  // Data members
  Subscription _subscriptions[MAX_SUBSCRIPTIONS];
  uint8_t _subTableSize;
  uint8_t _nextClientID;
  uint8_t _connectedClients[256]; // Track connected clients
  uint8_t _clientCount;
  
  // Client ID to Serial Number mapping
  ClientMapping _clientMappings[MAX_CLIENT_MAPPINGS];
  uint8_t _mappingCount;
  
  // Storage helpers
  #ifdef ESP32
  Preferences _preferences;
  #endif
  void initStorage();
  
  // Callbacks
  ConnectionCallback _onClientConnect;
  ConnectionCallback _onClientDisconnect;
  MessageCallback _onPublish;
  DirectMessageCallback _onDirectMessage;
};

// Pub/Sub Client class
class CANMqttClient : public CANMqttBase {
public:
  CANMqttClient(CANControllerClass& can);
  
  // Initialization
  bool begin(unsigned long timeout = 5000);
  bool begin(const String& serialNumber, unsigned long timeout = 5000);
  void end();
  
  // Connection
  bool connect(unsigned long timeout = 5000);
  bool connect(const String& serialNumber, unsigned long timeout = 5000);
  bool isConnected();
  uint8_t getClientId();
  String getSerialNumber();
  
  // Main loop processing
  void loop();
  
  // Message handling
  void handleMessage(int packetSize);
  
  // Pub/Sub operations
  bool subscribe(const String& topic);
  bool unsubscribe(const String& topic);
  bool publish(const String& topic, const String& message);
  bool sendDirectMessage(const String& message);
  bool ping();
  
  // Callbacks
  void onMessage(MessageCallback callback);
  void onDirectMessage(DirectMessageCallback callback);
  void onConnect(void (*callback)());
  void onDisconnect(void (*callback)());
  
  // Topic management
  bool isSubscribed(const String& topic);
  uint8_t getSubscriptionCount();
  
private:
  // ID management
  void requestClientID();
  void requestClientIDWithSerial(const String& serialNumber);
  
  // Message handlers
  void handleIdAssignment();
  void handleTopicData();
  void handleDirectMessageReceived();
  void handlePong();
  
  // Data members
  uint8_t _clientId;
  bool _connected;
  String _serialNumber;
  uint16_t _subscribedTopics[MAX_CLIENT_TOPICS];
  uint8_t _subscribedTopicCount;
  unsigned long _lastPing;
  unsigned long _lastPong;
  
  // Callbacks
  MessageCallback _onMessage;
  DirectMessageCallback _onDirectMessage;
  void (*_onConnect)();
  void (*_onDisconnect)();
};

#endif // CAN_MQTT_H
