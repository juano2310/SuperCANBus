// Copyright (c) Sandeep Mistry. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
// Extended with publish/subscribe protocol by Juan Pablo Risso

#ifndef CAN_H
#define CAN_H

// ESP32-C3, C6, H2 and other variants don't have built-in CAN controller
// They require an external controller like MCP2515
#if defined(ARDUINO_ARCH_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(CONFIG_IDF_TARGET_ESP32H2) && !defined(CONFIG_IDF_TARGET_ESP32C2)
  #include "ESP32SJA1000.h"
  // CAN object is defined in ESP32SJA1000.cpp
#else
  #include "MCP2515.h"
  // For non-ESP32 or ESP32-C3/C6/H2 variants, you need to create MCP2515Class object in your sketch
  // Example: MCP2515Class CAN;
#endif

// Include publish/subscribe protocol support
#include "CANMqtt.h"

#endif
