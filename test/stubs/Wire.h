// Signatures copied from arduino-esp32 3.1.0 libraries/Wire/src/Wire.h
#pragma once
#include "Arduino.h"
#define WIRE_HAS_BUFFER_SIZE 1
#define WIRE_HAS_END 1
class TwoWire : public Stream {
 public:
  TwoWire(uint8_t bus_num);
  bool begin() { return begin(-1, -1); }
  bool begin(uint8_t address);
  bool end();
  bool setClock(uint32_t freq);
  void beginTransmission(uint8_t address);
  uint8_t endTransmission(bool stopBit);
  uint8_t endTransmission();
  size_t requestFrom(uint8_t address, size_t len, bool stopBit);
  size_t requestFrom(uint8_t address, size_t len);
  bool setPins(int sda, int scl);
  bool begin(int sda, int scl, uint32_t frequency = 0);
  bool begin(uint8_t slaveAddr, int sda, int scl, uint32_t frequency);
  size_t setBufferSize(size_t bSize);
  void setTimeOut(uint16_t timeOutMillis);
  uint16_t getTimeOut();
  uint32_t getClock();
  size_t write(uint8_t) override;
  size_t write(const uint8_t*, size_t) override;
  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
};
extern TwoWire Wire;
extern TwoWire Wire1;
