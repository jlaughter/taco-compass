#pragma once
#include "Arduino.h"
#define SPI_MODE0 0
class SPISettings { public: SPISettings(uint32_t = 0, uint8_t = 0, uint8_t = 0) {} };
class SPIClass {
 public:
  void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1); void end();
  void beginTransaction(SPISettings); void endTransaction();
  uint8_t transfer(uint8_t); void transfer(void*, uint32_t); uint16_t transfer16(uint16_t);
  void transferBytes(const uint8_t*, uint8_t*, uint32_t); void writeBytes(const uint8_t*, uint32_t);
};
extern SPIClass SPI;
