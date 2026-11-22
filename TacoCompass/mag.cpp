#include "mag.h"

bool Magnetometer::wr(uint8_t reg, uint8_t val) {
  w_->beginTransmission(addr_);
  w_->write(reg);
  w_->write(val);
  return w_->endTransmission() == 0;
}

int Magnetometer::rd(uint8_t reg) {
  uint8_t v;
  return rdn(reg, &v, 1) ? v : -1;
}

bool Magnetometer::rdn(uint8_t reg, uint8_t* buf, size_t n) {
  w_->beginTransmission(addr_);
  w_->write(reg);
  if (w_->endTransmission(false) != 0) return false;
  if (w_->requestFrom(addr_, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = w_->read();
  return true;
}

const char* Magnetometer::name() const {
  switch (chip_) {
    case MagChip::LIS3MDL: return "LIS3MDL";
    case MagChip::MMC5603: return "MMC5603";
    case MagChip::QMC5883L: return "QMC5883L";
    case MagChip::QMC5883P: return "QMC5883P";
    case MagChip::HMC5883L: return "HMC5883L";
    default: return "none";
  }
}

bool Magnetometer::begin(TwoWire& w) {
  w_ = &w;
  chip_ = MagChip::None;

  for (uint8_t a : {(uint8_t)0x1C, (uint8_t)0x1E}) {
    addr_ = a;
    if (rd(0x0F) == 0x3D) {
      chip_ = MagChip::LIS3MDL;
      wr(0x20, 0x7C);  // CTRL1: X/Y ultra-high-perf, 80 Hz
      wr(0x21, 0x00);  // CTRL2: ±4 gauss
      wr(0x22, 0x00);  // CTRL3: continuous
      wr(0x23, 0x0C);  // CTRL4: Z ultra-high-perf
      wr(0x24, 0x40);  // CTRL5: block data update
      return true;
    }
  }

  addr_ = 0x1E;  // HMC5883L: identification registers 0x0A..0x0C read "H43"
  if (rd(0x0A) == 'H' && rd(0x0B) == '4' && rd(0x0C) == '3') {
    chip_ = MagChip::HMC5883L;
    wr(0x00, 0x70);  // CRA: 8-sample average, 15 Hz
    wr(0x01, 0x20);  // CRB: ±1.3 gauss, 1090 LSB/gauss
    wr(0x02, 0x00);  // mode: continuous
    return true;
  }

  addr_ = 0x30;
  if (rd(0x39) == 0x10) {
    chip_ = MagChip::MMC5603;
    wr(0x1C, 0x80);  // CTRL1: software reset
    delay(20);
    wr(0x1B, 0x08);  // SET pulse
    delay(1);
    wr(0x1B, 0x10);  // RESET pulse
    delay(1);
    wr(0x1A, 75);    // ODR 75 Hz
    wr(0x1B, 0xA0);  // CTRL0: cmm_freq_en | auto_sr_en
    wr(0x1D, 0x10);  // CTRL2: continuous mode
    return true;
  }

  addr_ = 0x0D;
  if (rd(0x0D) == 0xFF) {
    chip_ = MagChip::QMC5883L;
    wr(0x0B, 0x01);  // SET/RESET period
    wr(0x09, 0x1D);  // OSR 512, ±8 G, 200 Hz, continuous
    return true;
  }

  addr_ = 0x2C;
  if (rd(0x00) == 0x80) {
    chip_ = MagChip::QMC5883P;
    wr(0x29, 0x06);  // axis sign definition (per vendor app note)
    wr(0x0B, 0x08);  // CTRL2: set/reset on, ±8 G
    wr(0x0A, 0xCD);  // CTRL1: normal mode, 200 Hz, OSR
    return true;
  }
  return false;
}

static inline int16_t le16(const uint8_t* p) { return (int16_t)(p[0] | (p[1] << 8)); }

bool Magnetometer::read(Vec3& uT) {
  uint8_t d[6];
  switch (chip_) {
    case MagChip::LIS3MDL:
      if (!rdn(0x28 | 0x80, d, 6)) return false;  // 0x80 = auto-increment
      uT = {le16(d) * (100.0f / 6842), le16(d + 2) * (100.0f / 6842), le16(d + 4) * (100.0f / 6842)};
      return true;
    case MagChip::MMC5603: {
      if (!rdn(0x00, d, 6)) return false;
      // 16-bit mode: unsigned, 32768 = zero, 1024 counts/gauss
      auto be = [](const uint8_t* p) { return (int32_t)((p[0] << 8) | p[1]) - 32768; };
      uT = {be(d) * (100.0f / 1024), be(d + 2) * (100.0f / 1024), be(d + 4) * (100.0f / 1024)};
      return true;
    }
    case MagChip::QMC5883L:
      if (!rdn(0x00, d, 6)) return false;
      uT = {le16(d) * (100.0f / 3000), le16(d + 2) * (100.0f / 3000), le16(d + 4) * (100.0f / 3000)};
      return true;
    case MagChip::QMC5883P:
      if (!rdn(0x01, d, 6)) return false;
      uT = {le16(d) * (100.0f / 3750), le16(d + 2) * (100.0f / 3750), le16(d + 4) * (100.0f / 3750)};
      return true;
    case MagChip::HMC5883L: {
      if (!rdn(0x03, d, 6)) return false;
      // big-endian, register order X, Z, Y
      auto be16 = [](const uint8_t* p) { return (int16_t)((p[0] << 8) | p[1]); };
      const float k = 100.0f / 1090;
      uT = {be16(d) * k, be16(d + 4) * k, be16(d + 2) * k};
      return true;
    }
    default:
      return false;
  }
}
