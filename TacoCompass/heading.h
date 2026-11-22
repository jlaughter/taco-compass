// Magnetometer calibration, tilt-compensated heading and smoothing.
// Board frame: +X right, +Y up (12 o'clock), +Z out of the screen.
#pragma once
#include <stdint.h>
#include <stdbool.h>

struct Vec3 { float x, y, z; };

struct MagCal {
  float offset[3] = {0, 0, 0};   // hard-iron, µT
  float scale[3] = {1, 1, 1};    // diagonal soft-iron
  bool valid = false;

  Vec3 apply(const Vec3& raw) const;
};

// Min/max calibrator with an octant-coverage score for the progress UI.
class MagCalibrator {
 public:
  void reset();
  void add(const Vec3& raw);
  float coverage() const;          // 0..1, how much of the sphere has been seen
  float minRange() const;          // smallest axis span, µT
  bool finish(MagCal& out) const;  // false if the tumble wasn't good enough
  int samples() const { return n_; }

 private:
  float mn_[3], mx_[3];
  uint8_t bins_[2][2][2];
  int n_ = 0;
};

// Heading of the board's +Y axis, degrees clockwise from MAGNETIC north.
// accel: specific force (reads +1 g on +Z when lying screen-up).
// mag:   calibrated field, µT. Returns false if the geometry is degenerate.
bool tilt_heading(const Vec3& accel, const Vec3& mag, float& heading_deg);

// Exponential smoothing on the unit circle (handles the 359->0 wrap).
class AngleSmoother {
 public:
  explicit AngleSmoother(float alpha) : a_(alpha) {}
  float update(float deg);
  float value() const;
  void reset() { init_ = false; }
  void setAlpha(float a) { a_ = a; }

 private:
  float a_, s_ = 0, c_ = 1;
  bool init_ = false;
};

// Bubble-level angles from the accelerometer (board frame, reads +1 g on +Z flat).
// rollDeg  > 0 when the 3 o'clock edge is raised; pitchDeg > 0 when 12 o'clock is raised.
// tiltDeg is the total angle between the screen normal and vertical.
// Returns false if the reading is unusable (free fall).
bool level_angles(const Vec3& accel, float& rollDeg, float& pitchDeg, float& tiltDeg);

// "N", "NNE", "NE", ... for a heading in degrees.
const char* cardinal16(float deg);
