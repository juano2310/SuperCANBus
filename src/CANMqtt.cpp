// CANMqtt.cpp - Publish/Subscribe protocol for CAN bus
// Part of Super CAN+ Library

#include "CANMqtt.h"

// ===== CANMqttBase Implementation =====

CANMqttBase::CANMqttBase(CANControllerClass& can) : _can(&can), _topicMappingCount(0) {
}

uint16_t CANMqttBase::hashTopic(const String& topic) {
  uint16_t hash = 0;
  for (unsigned int i = 0; i < topic.length(); i++) {
    hash = hash * 31 + topic.charAt(i);
  }
  return hash;
}

void CANMqttBase::registerTopic(const String& topic) {
  uint16_t hash = hashTopic(topic);
  
  // Check if already registered
  for (uint8_t i = 0; i < _topicMappingCount; i++) {
    if (_topicMappings[i].hash == hash) {
      return; // Already registered
    }
  }
  
  // Add new mapping
  if (_topicMappingCount < MAX_SUBSCRIPTIONS) {
    _topicMappings[_topicMappingCount].hash = hash;
    _topicMappings[_topicMappingCount].name = topic;
    _topicMappingCount++;
  }
}

String CANMqttBase::getTopicName(uint16_t hash) {
  for (uint8_t i = 0; i < _topicMappingCount; i++) {
    if (_topicMappings[i].hash == hash) {
      return _topicMappings[i].name;
    }
  }
  return String("0x") + String(hash, HEX);
}

// ===== CANMqttBroker Implementation =====

CANMqttBroker::CANMqttBroker(CANControllerClass& can) 
  : CANMqttBase(can),
    _subTableSize(0),
    _nextClientID(0x10),
    _clientCount(0),
    _mappingCount(0),
    _onClientConnect(nullptr),
    _onClientDisconnect(nullptr),
    _onPublish(nullptr),
    _onDirectMessage(nullptr) {
  memset(_subscriptions, 0, sizeof(_subscriptions));
  memset(_connectedClients, 0, sizeof(_connectedClients));
  memset(_clientMappings, 0, sizeof(_clientMappings));
}

bool CANMqttBroker::begin() {
  _subTableSize = 0;
  _nextClientID = 0x10;
  _clientCount = 0;
  _mappingCount = 0;
  
  // Initialize storage and load saved mappings
  initStorage();
  loadMappingsFromStorage();
  
  return true;
}

void CANMqttBroker::end() {
  _subTableSize = 0;
  _clientCount = 0;
}

void CANMqttBroker::loop() {
  int packetSize = _can->parsePacket();
  if (packetSize > 0) {
    handleMessage(packetSize);
  }
}

void CANMqttBroker::handleMessage(int packetSize) {
  uint8_t msgType = _can->packetId();
  
  switch (msgType) {
    case CAN_MQTT_SUBSCRIBE:
      handleSubscribe();
      break;
    case CAN_MQTT_UNSUBSCRIBE:
      handleUnsubscribe();
      break;
    case CAN_MQTT_PUBLISH:
      handlePublish();
      break;
    case CAN_MQTT_DIRECT_MSG:
      handleDirectMessage();
      break;
    case CAN_MQTT_PING:
      handlePing();
      break;
    case CAN_MQTT_ID_REQUEST:
      // Check if this is a request with serial number (has data)
      if (_can->available() > 0) {
        handleIdRequestWithSerial();
      } else {
        assignClientID(); // Old method for backward compatibility
      }
      break;
  }
}

void CANMqttBroker::handleSubscribe() {
  if (_can->available() < 3) return;
  
  uint8_t clientId = _can->read();
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  
  // Read topic name if available (for broker-side topic mapping)
  String topicName = "";
  if (_can->available() > 0) {
    uint8_t topicLen = _can->read();
    for (uint8_t i = 0; i < topicLen && _can->available(); i++) {
      topicName += (char)_can->read();
    }
  }
  
  // Register topic name if provided
  if (topicName.length() > 0) {
    registerTopic(topicName);
  }
  
  addSubscription(clientId, topicHash);
  
  // Track connected client
  bool found = false;
  for (uint8_t i = 0; i < _clientCount; i++) {
    if (_connectedClients[i] == clientId) {
      found = true;
      break;
    }
  }
  if (!found && _clientCount < 256) {
    _connectedClients[_clientCount++] = clientId;
    if (_onClientConnect) {
      _onClientConnect(clientId);
    }
  }
}

void CANMqttBroker::handleUnsubscribe() {
  if (_can->available() < 3) return;
  
  uint8_t clientId = _can->read();
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  
  removeSubscription(clientId, topicHash);
}

void CANMqttBroker::handlePublish() {
  if (_can->available() < 3) return;
  
  uint8_t publisherId = _can->read();
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  
  // Get topic name from stored mapping (learned from SUBSCRIBE)
  String topicName = getTopicName(topicHash);
  
  // Read message (all remaining data)
  String message = "";
  while (_can->available()) {
    message += (char)_can->read();
  }
  
  // Call callback if registered
  if (_onPublish) {
    _onPublish(topicHash, topicName, message);
  }
  
  // Forward to subscribers
  forwardToSubscribers(topicHash, message);
}

void CANMqttBroker::handleDirectMessage() {
  if (_can->available() < 1) return;
  
  uint8_t senderId = _can->read();
  
  String message = "";
  while (_can->available()) {
    message += (char)_can->read();
  }
  
  // Call callback if registered
  if (_onDirectMessage) {
    _onDirectMessage(senderId, message);
  }
  
  // Send acknowledgment
  _can->beginPacket(CAN_MQTT_ACK);
  _can->write(CAN_MQTT_BROKER_ID);
  _can->write(senderId);
  _can->print("ACK");
  _can->endPacket();
}

void CANMqttBroker::handlePing() {
  if (_can->available() < 1) return;
  
  uint8_t clientId = _can->read();
  
  // Send pong response
  _can->beginPacket(CAN_MQTT_PONG);
  _can->write(CAN_MQTT_BROKER_ID);
  _can->write(clientId);
  _can->endPacket();
}

void CANMqttBroker::addSubscription(uint8_t clientId, uint16_t topicHash) {
  // Find or create topic entry
  for (uint8_t i = 0; i < _subTableSize; i++) {
    if (_subscriptions[i].topicHash == topicHash) {
      // Add client if not already subscribed
      for (uint8_t j = 0; j < _subscriptions[i].subCount; j++) {
        if (_subscriptions[i].subscribers[j] == clientId) return;
      }
      if (_subscriptions[i].subCount < MAX_SUBSCRIBERS_PER_TOPIC) {
        _subscriptions[i].subscribers[_subscriptions[i].subCount++] = clientId;
      }
      return;
    }
  }
  
  // Create new topic entry
  if (_subTableSize < MAX_SUBSCRIPTIONS) {
    _subscriptions[_subTableSize].topicHash = topicHash;
    _subscriptions[_subTableSize].subscribers[0] = clientId;
    _subscriptions[_subTableSize].subCount = 1;
    _subTableSize++;
  }
}

void CANMqttBroker::removeSubscription(uint8_t clientId, uint16_t topicHash) {
  for (uint8_t i = 0; i < _subTableSize; i++) {
    if (_subscriptions[i].topicHash == topicHash) {
      for (uint8_t j = 0; j < _subscriptions[i].subCount; j++) {
        if (_subscriptions[i].subscribers[j] == clientId) {
          // Shift remaining subscribers
          for (uint8_t k = j; k < _subscriptions[i].subCount - 1; k++) {
            _subscriptions[i].subscribers[k] = _subscriptions[i].subscribers[k + 1];
          }
          _subscriptions[i].subCount--;
          return;
        }
      }
    }
  }
}

void CANMqttBroker::removeAllSubscriptions(uint8_t clientId) {
  for (uint8_t i = 0; i < _subTableSize; i++) {
    for (uint8_t j = 0; j < _subscriptions[i].subCount; j++) {
      if (_subscriptions[i].subscribers[j] == clientId) {
        // Shift remaining subscribers
        for (uint8_t k = j; k < _subscriptions[i].subCount - 1; k++) {
          _subscriptions[i].subscribers[k] = _subscriptions[i].subscribers[k + 1];
        }
        _subscriptions[i].subCount--;
        j--; // Check the same index again
      }
    }
  }
}

void CANMqttBroker::forwardToSubscribers(uint16_t topicHash, const String& message) {
  for (uint8_t i = 0; i < _subTableSize; i++) {
    if (_subscriptions[i].topicHash == topicHash) {
      for (uint8_t j = 0; j < _subscriptions[i].subCount; j++) {
        uint8_t subId = _subscriptions[i].subscribers[j];
        
        _can->beginPacket(CAN_MQTT_TOPIC_DATA);
        _can->write(subId);
        _can->write(topicHash >> 8);
        _can->write(topicHash & 0xFF);
        _can->print(message);
        _can->endPacket();
        
        delay(10); // Small delay between sends
      }
      return;
    }
  }
}

void CANMqttBroker::assignClientID() {
  _can->beginPacket(CAN_MQTT_ID_RESPONSE);
  _can->write(_nextClientID);
  _can->endPacket();
  
  _nextClientID++;
  if (_nextClientID == 0xFF) {
    _nextClientID = 0x10; // Wrap around, skip special IDs
  }
}

void CANMqttBroker::onClientConnect(ConnectionCallback callback) {
  _onClientConnect = callback;
}

void CANMqttBroker::onClientDisconnect(ConnectionCallback callback) {
  _onClientDisconnect = callback;
}

void CANMqttBroker::onPublish(MessageCallback callback) {
  _onPublish = callback;
}

void CANMqttBroker::onDirectMessage(DirectMessageCallback callback) {
  _onDirectMessage = callback;
}

void CANMqttBroker::sendToClient(uint8_t clientId, uint16_t topicHash, const String& message) {
  _can->beginPacket(CAN_MQTT_TOPIC_DATA);
  _can->write(clientId);
  _can->write(topicHash >> 8);
  _can->write(topicHash & 0xFF);
  _can->print(message);
  _can->endPacket();
}

void CANMqttBroker::sendDirectMessage(uint8_t clientId, const String& message) {
  _can->beginPacket(CAN_MQTT_DIRECT_MSG);
  _can->write(CAN_MQTT_BROKER_ID);
  _can->write(clientId);
  _can->print(message);
  _can->endPacket();
}

void CANMqttBroker::broadcastMessage(uint16_t topicHash, const String& message) {
  forwardToSubscribers(topicHash, message);
}

uint8_t CANMqttBroker::getClientCount() {
  return _clientCount;
}

uint8_t CANMqttBroker::getSubscriptionCount() {
  return _subTableSize;
}

void CANMqttBroker::getSubscribers(uint16_t topicHash, uint8_t* subscribers, uint8_t* count) {
  for (uint8_t i = 0; i < _subTableSize; i++) {
    if (_subscriptions[i].topicHash == topicHash) {
      *count = _subscriptions[i].subCount;
      memcpy(subscribers, _subscriptions[i].subscribers, _subscriptions[i].subCount);
      return;
    }
  }
  *count = 0;
}

void CANMqttBroker::listSubscribedTopics(std::function<void(uint16_t hash, const String& name, uint8_t subscriberCount)> callback) {
  if (!callback) return;
  
  for (uint8_t i = 0; i < _subTableSize; i++) {
    uint16_t hash = _subscriptions[i].topicHash;
    String name = getTopicName(hash);
    uint8_t count = _subscriptions[i].subCount;
    callback(hash, name, count);
  }
}

// ===== Client ID Mapping Methods =====

void CANMqttBroker::handleIdRequestWithSerial() {
  // Read serial number from CAN message
  String serialNumber = "";
  while (_can->available()) {
    serialNumber += (char)_can->read();
  }
  
  if (serialNumber.length() == 0) {
    // No serial number provided, use old method
    assignClientID();
    return;
  }
  
  // Find or create client ID for this serial number
  uint8_t assignedId = findOrCreateClientId(serialNumber);
  
  // Send response
  _can->beginPacket(CAN_MQTT_ID_RESPONSE);
  _can->write(assignedId);
  _can->endPacket();
}

uint8_t CANMqttBroker::findOrCreateClientId(const String& serialNumber) {
  // Check if this serial number already has an ID
  int index = findClientMapping(serialNumber);
  
  if (index >= 0) {
    // Found existing mapping, mark as active and return the same ID
    _clientMappings[index].active = true;
    saveMappingsToStorage(); // Save state change
    return _clientMappings[index].clientId;
  }
  
  // No existing mapping, create a new one
  if (_mappingCount < MAX_CLIENT_MAPPINGS) {
    _clientMappings[_mappingCount].clientId = _nextClientID;
    _clientMappings[_mappingCount].setSerial(serialNumber);
    _clientMappings[_mappingCount].active = true;
    _mappingCount++;
    
    uint8_t assignedId = _nextClientID;
    
    // Increment for next client
    _nextClientID++;
    if (_nextClientID == 0xFF) {
      _nextClientID = 0x10; // Wrap around, skip special IDs
    }
    
    saveMappingsToStorage(); // Save new mapping
    return assignedId;
  }
  
  // Mapping table full, return error (use 0xFF)
  return CAN_MQTT_UNASSIGNED_ID;
}

int CANMqttBroker::findClientMapping(const String& serialNumber) {
  for (uint8_t i = 0; i < _mappingCount; i++) {
    if (_clientMappings[i].getSerial() == serialNumber) {
      return i;
    }
  }
  return -1;
}

int CANMqttBroker::findClientMappingById(uint8_t clientId) {
  for (uint8_t i = 0; i < _mappingCount; i++) {
    if (_clientMappings[i].clientId == clientId) {
      return i;
    }
  }
  return -1;
}

uint8_t CANMqttBroker::registerClient(const String& serialNumber) {
  return findOrCreateClientId(serialNumber);
}

bool CANMqttBroker::unregisterClient(uint8_t clientId) {
  int index = findClientMappingById(clientId);
  if (index >= 0) {
    _clientMappings[index].active = false;
    // Remove all subscriptions for this client
    removeAllSubscriptions(clientId);
    saveMappingsToStorage(); // Save state change
    return true;
  }
  return false;
}

bool CANMqttBroker::unregisterClientBySerial(const String& serialNumber) {
  int index = findClientMapping(serialNumber);
  if (index >= 0) {
    _clientMappings[index].active = false;
    // Remove all subscriptions for this client
    removeAllSubscriptions(_clientMappings[index].clientId);
    saveMappingsToStorage(); // Save state change
    return true;
  }
  return false;
}

uint8_t CANMqttBroker::getClientIdBySerial(const String& serialNumber) {
  int index = findClientMapping(serialNumber);
  if (index >= 0) {
    return _clientMappings[index].clientId;
  }
  return CAN_MQTT_UNASSIGNED_ID;
}

String CANMqttBroker::getSerialByClientId(uint8_t clientId) {
  int index = findClientMappingById(clientId);
  if (index >= 0) {
    return _clientMappings[index].getSerial();
  }
  return "";
}

bool CANMqttBroker::updateClientSerial(uint8_t clientId, const String& newSerial) {
  int index = findClientMappingById(clientId);
  if (index >= 0) {
    // Check if new serial already exists
    if (findClientMapping(newSerial) >= 0) {
      return false; // Serial already in use
    }
    _clientMappings[index].setSerial(newSerial);
    saveMappingsToStorage(); // Save change
    return true;
  }
  return false;
}

uint8_t CANMqttBroker::getRegisteredClientCount() {
  return _mappingCount;
}

void CANMqttBroker::listRegisteredClients(std::function<void(uint8_t id, const String& serial, bool active)> callback) {
  if (!callback) return;
  
  for (uint8_t i = 0; i < _mappingCount; i++) {
    callback(_clientMappings[i].clientId, 
             _clientMappings[i].getSerial(), 
             _clientMappings[i].active);
  }
}

// ===== CANMqttClient Implementation =====

CANMqttClient::CANMqttClient(CANControllerClass& can)
  : CANMqttBase(can),
    _clientId(CAN_MQTT_UNASSIGNED_ID),
    _connected(false),
    _subscribedTopicCount(0),
    _lastPing(0),
    _lastPong(0),
    _onMessage(nullptr),
    _onDirectMessage(nullptr),
    _onConnect(nullptr),
    _onDisconnect(nullptr) {
  memset(_subscribedTopics, 0, sizeof(_subscribedTopics));
}

bool CANMqttClient::begin(unsigned long timeout) {
  return connect(timeout);
}

bool CANMqttClient::begin(const String& serialNumber, unsigned long timeout) {
  return connect(serialNumber, timeout);
}

void CANMqttClient::end() {
  _connected = false;
  _clientId = CAN_MQTT_UNASSIGNED_ID;
  _subscribedTopicCount = 0;
  _serialNumber = "";
}

bool CANMqttClient::connect(unsigned long timeout) {
  requestClientID();
  
  unsigned long startTime = millis();
  while (_clientId == CAN_MQTT_UNASSIGNED_ID && (millis() - startTime) < timeout) {
    int packetSize = _can->parsePacket();
    if (packetSize > 0) {
      handleMessage(packetSize);
    }
    delay(10);
  }
  
  if (_clientId != CAN_MQTT_UNASSIGNED_ID) {
    _connected = true;
    if (_onConnect) {
      _onConnect();
    }
    return true;
  }
  
  return false;
}

bool CANMqttClient::connect(const String& serialNumber, unsigned long timeout) {
  _serialNumber = serialNumber;
  requestClientIDWithSerial(serialNumber);
  
  unsigned long startTime = millis();
  while (_clientId == CAN_MQTT_UNASSIGNED_ID && (millis() - startTime) < timeout) {
    int packetSize = _can->parsePacket();
    if (packetSize > 0) {
      handleMessage(packetSize);
    }
    delay(10);
  }
  
  if (_clientId != CAN_MQTT_UNASSIGNED_ID) {
    _connected = true;
    if (_onConnect) {
      _onConnect();
    }
    return true;
  }
  
  return false;
}

bool CANMqttClient::isConnected() {
  return _connected;
}

uint8_t CANMqttClient::getClientId() {
  return _clientId;
}

String CANMqttClient::getSerialNumber() {
  return _serialNumber;
}

void CANMqttClient::loop() {
  int packetSize = _can->parsePacket();
  if (packetSize > 0) {
    handleMessage(packetSize);
  }
}

void CANMqttClient::handleMessage(int packetSize) {
  uint8_t msgType = _can->packetId();
  
  switch (msgType) {
    case CAN_MQTT_ID_RESPONSE:
      handleIdAssignment();
      break;
    case CAN_MQTT_TOPIC_DATA:
      handleTopicData();
      break;
    case CAN_MQTT_DIRECT_MSG:
      handleDirectMessageReceived();
      break;
    case CAN_MQTT_PONG:
      handlePong();
      break;
    case CAN_MQTT_ACK:
      // Acknowledgment received
      break;
  }
}

void CANMqttClient::handleIdAssignment() {
  if (_can->available() < 1) return;
  
  _clientId = _can->read();
  _connected = true;
}

void CANMqttClient::handleTopicData() {
  if (_can->available() < 3) return;
  
  uint8_t targetId = _can->read();
  if (targetId != _clientId) return;
  
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  
  String message = "";
  while (_can->available()) {
    message += (char)_can->read();
  }
  
  // Call callback if registered
  if (_onMessage) {
    String topicName = getTopicName(topicHash);
    _onMessage(topicHash, topicName, message);
  }
}

void CANMqttClient::handleDirectMessageReceived() {
  if (_can->available() < 2) return;
  
  uint8_t senderId = _can->read();
  uint8_t targetId = _can->read();
  
  if (targetId != _clientId) return;
  
  String message = "";
  while (_can->available()) {
    message += (char)_can->read();
  }
  
  // Call callback if registered
  if (_onDirectMessage) {
    _onDirectMessage(senderId, message);
  }
}

void CANMqttClient::handlePong() {
  if (_can->available() < 2) return;
  
  uint8_t brokerId = _can->read();
  uint8_t targetId = _can->read();
  
  if (targetId == _clientId) {
    _lastPong = millis();
  }
}

void CANMqttClient::requestClientID() {
  _can->beginPacket(CAN_MQTT_ID_REQUEST);
  _can->endPacket();
}

void CANMqttClient::requestClientIDWithSerial(const String& serialNumber) {
  _can->beginPacket(CAN_MQTT_ID_REQUEST);
  _can->print(serialNumber);
  _can->endPacket();
}

bool CANMqttClient::subscribe(const String& topic) {
  if (!_connected) return false;
  
  uint16_t topicHash = hashTopic(topic);
  registerTopic(topic);
  
  _can->beginPacket(CAN_MQTT_SUBSCRIBE);
  _can->write(_clientId);
  _can->write(topicHash >> 8);
  _can->write(topicHash & 0xFF);
  _can->write((uint8_t)topic.length());  // Send topic name length
  _can->print(topic);  // Send topic name to broker for mapping
  _can->endPacket();
  
  // Store locally
  if (_subscribedTopicCount < MAX_CLIENT_TOPICS) {
    _subscribedTopics[_subscribedTopicCount++] = topicHash;
  }
  
  return true;
}

bool CANMqttClient::unsubscribe(const String& topic) {
  if (!_connected) return false;
  
  uint16_t topicHash = hashTopic(topic);
  
  _can->beginPacket(CAN_MQTT_UNSUBSCRIBE);
  _can->write(_clientId);
  _can->write(topicHash >> 8);
  _can->write(topicHash & 0xFF);
  _can->endPacket();
  
  // Remove from local list
  for (uint8_t i = 0; i < _subscribedTopicCount; i++) {
    if (_subscribedTopics[i] == topicHash) {
      for (uint8_t j = i; j < _subscribedTopicCount - 1; j++) {
        _subscribedTopics[j] = _subscribedTopics[j + 1];
      }
      _subscribedTopicCount--;
      break;
    }
  }
  
  return true;
}

bool CANMqttClient::publish(const String& topic, const String& message) {
  if (!_connected) return false;
  
  uint16_t topicHash = hashTopic(topic);
  registerTopic(topic);
  
  _can->beginPacket(CAN_MQTT_PUBLISH);
  _can->write(_clientId);
  _can->write(topicHash >> 8);
  _can->write(topicHash & 0xFF);
  _can->print(message);
  _can->endPacket();
  
  return true;
}

bool CANMqttClient::sendDirectMessage(const String& message) {
  if (!_connected) return false;
  
  _can->beginPacket(CAN_MQTT_DIRECT_MSG);
  _can->write(_clientId);
  _can->print(message);
  _can->endPacket();
  
  return true;
}

bool CANMqttClient::ping() {
  if (!_connected) return false;
  
  _can->beginPacket(CAN_MQTT_PING);
  _can->write(_clientId);
  _can->endPacket();
  
  _lastPing = millis();
  
  return true;
}

void CANMqttClient::onMessage(MessageCallback callback) {
  _onMessage = callback;
}

void CANMqttClient::onDirectMessage(DirectMessageCallback callback) {
  _onDirectMessage = callback;
}

void CANMqttClient::onConnect(void (*callback)()) {
  _onConnect = callback;
}

void CANMqttClient::onDisconnect(void (*callback)()) {
  _onDisconnect = callback;
}

bool CANMqttClient::isSubscribed(const String& topic) {
  uint16_t topicHash = hashTopic(topic);
  for (uint8_t i = 0; i < _subscribedTopicCount; i++) {
    if (_subscribedTopics[i] == topicHash) {
      return true;
    }
  }
  return false;
}

uint8_t CANMqttClient::getSubscriptionCount() {
  return _subscribedTopicCount;
}

// ===== Persistent Storage Implementation =====

void CANMqttBroker::initStorage() {
  #ifdef ESP32
    // ESP32 uses Preferences (NVS)
    _preferences.begin(STORAGE_NAMESPACE, false);
  #else
    // Arduino uses EEPROM
    EEPROM.begin(EEPROM_SIZE);
  #endif
}

bool CANMqttBroker::loadMappingsFromStorage() {
  #ifdef ESP32
    // ESP32 implementation using Preferences
    uint16_t magic = _preferences.getUShort("magic", 0);
    if (magic != STORAGE_MAGIC) {
      // No valid data stored
      return false;
    }
    
    _mappingCount = _preferences.getUChar("count", 0);
    _nextClientID = _preferences.getUChar("nextID", 0x10);
    
    if (_mappingCount > MAX_CLIENT_MAPPINGS) {
      _mappingCount = 0;
      return false;
    }
    
    // Load each mapping
    for (uint8_t i = 0; i < _mappingCount; i++) {
      String key = "map" + String(i);
      size_t len = _preferences.getBytesLength(key.c_str());
      if (len == sizeof(ClientMapping)) {
        _preferences.getBytes(key.c_str(), &_clientMappings[i], sizeof(ClientMapping));
      }
    }
    
    return true;
    
  #else
    // Arduino EEPROM implementation
    uint16_t magic;
    EEPROM.get(0, magic);
    
    if (magic != STORAGE_MAGIC) {
      // No valid data stored
      return false;
    }
    
    int addr = sizeof(uint16_t);
    EEPROM.get(addr, _mappingCount);
    addr += sizeof(uint8_t);
    
    EEPROM.get(addr, _nextClientID);
    addr += sizeof(uint8_t);
    
    if (_mappingCount > MAX_CLIENT_MAPPINGS) {
      _mappingCount = 0;
      return false;
    }
    
    // Load each mapping
    for (uint8_t i = 0; i < _mappingCount; i++) {
      EEPROM.get(addr, _clientMappings[i]);
      addr += sizeof(ClientMapping);
    }
    
    return true;
  #endif
}

bool CANMqttBroker::saveMappingsToStorage() {
  #ifdef ESP32
    // ESP32 implementation using Preferences
    _preferences.putUShort("magic", STORAGE_MAGIC);
    _preferences.putUChar("count", _mappingCount);
    _preferences.putUChar("nextID", _nextClientID);
    
    // Save each mapping
    for (uint8_t i = 0; i < _mappingCount; i++) {
      String key = "map" + String(i);
      _preferences.putBytes(key.c_str(), &_clientMappings[i], sizeof(ClientMapping));
    }
    
    return true;
    
  #else
    // Arduino EEPROM implementation
    int addr = 0;
    
    // Write magic number
    EEPROM.put(addr, STORAGE_MAGIC);
    addr += sizeof(uint16_t);
    
    // Write count and next ID
    EEPROM.put(addr, _mappingCount);
    addr += sizeof(uint8_t);
    
    EEPROM.put(addr, _nextClientID);
    addr += sizeof(uint8_t);
    
    // Write each mapping
    for (uint8_t i = 0; i < _mappingCount; i++) {
      EEPROM.put(addr, _clientMappings[i]);
      addr += sizeof(ClientMapping);
    }
    
    #if defined(ESP8266)
      EEPROM.commit();
    #endif
    
    return true;
  #endif
}

bool CANMqttBroker::clearStoredMappings() {
  _mappingCount = 0;
  _nextClientID = 0x10;
  memset(_clientMappings, 0, sizeof(_clientMappings));
  
  #ifdef ESP32
    _preferences.clear();
    return true;
  #else
    // Clear EEPROM by writing 0 to magic number
    uint16_t zero = 0;
    EEPROM.put(0, zero);
    #if defined(ESP8266)
      EEPROM.commit();
    #endif
    return true;
  #endif
}
