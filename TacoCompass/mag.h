// External magnetometer on its own I2C bus (Wire1). Auto-detects:
//   LIS3MDL  (0x1C / 0x1E)  — Adafruit #4479, recommended
//   MMC5603  (0x30)          — Adafruit #5579
//   QMC5883L (0x0D)          — common "GY-271" modules
//   QMC5883P (0x2C)          — newer "GY-271" modules (less tested)
//   HMC5883L (0x1E)          — the original Honeywell part (rare now; GY-271/273 boards
//                              labeled "HMC5883L" usually carry a QMC5883L instead)
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "heading.h"

enum class MagChip : uint8_t { None, LIS3MDL, MMC5603, QMC5883L, QMC5883P, HMC5883L };

class Magnetometer {
 public:
  bool begin(TwoWire& w);
  bool read(Vec3& microTesla);   // sensor-frame, µT; false on bus error
  MagChip chip() const { return chip_; }
  const char* name() const;

 private:
  TwoWire* w_ = nullptr;
  MagChip chip_ = MagChip::None;
  uint8_t addr_ = 0;
  bool wr(uint8_t reg, uint8_t val);
  int rd(uint8_t reg);
  bool rdn(uint8_t reg, uint8_t* buf, size_t n);
};
