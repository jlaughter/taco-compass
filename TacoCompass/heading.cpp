#include "heading.h"
#include <math.h>
#include <string.h>

static const float kRad2Deg = 57.29577951f;
static const float kMinUsefulRangeUT = 30.0f;  // Earth field is ~25-65 µT, so a full
                                               // tumble spans ~50-130 µT per axis

Vec3 MagCal::apply(const Vec3& r) const {
  return {(r.x - offset[0]) * scale[0], (r.y - offset[1]) * scale[1],
          (r.z - offset[2]) * scale[2]};
}

void MagCalibrator::reset() {
  for (int i = 0; i < 3; i++) { mn_[i] = 1e9f; mx_[i] = -1e9f; }
  memset(bins_, 0, sizeof(bins_));
  n_ = 0;
}

void MagCalibrator::add(const Vec3& r) {
  const float v[3] = {r.x, r.y, r.z};
  for (int i = 0; i < 3; i++) {
    if (v[i] < mn_[i]) mn_[i] = v[i];
    if (v[i] > mx_[i]) mx_[i] = v[i];
  }
  n_++;
  if (n_ > 10) {  // classify against the running centre once it means something
    int b[3];
    for (int i = 0; i < 3; i++) b[i] = v[i] > (mn_[i] + mx_[i]) * 0.5f ? 1 : 0;
    bins_[b[0]][b[1]][b[2]] = 1;
  }
}

float MagCalibrator::coverage() const {
  int seen = 0;
  for (int a = 0; a < 2; a++)
    for (int b = 0; b < 2; b++)
      for (int c = 0; c < 2; c++) seen += bins_[a][b][c];
  float octants = seen / 8.0f;
  float spread = minRange() / 60.0f;
  if (spread > 1) spread = 1;
  if (spread < 0) spread = 0;
  return 0.5f * octants + 0.5f * spread;
}

float MagCalibrator::minRange() const {
  if (n_ == 0) return 0;
  float r = 1e9f;
  for (int i = 0; i < 3; i++) {
    float d = mx_[i] - mn_[i];
    if (d < r) r = d;
  }
  return r;
}

bool MagCalibrator::finish(MagCal& out) const {
  if (n_ < 50 || minRange() < kMinUsefulRangeUT) return false;
  float range[3], avg = 0;
  for (int i = 0; i < 3; i++) {
    out.offset[i] = (mx_[i] + mn_[i]) * 0.5f;
    range[i] = (mx_[i] - mn_[i]) * 0.5f;
    avg += range[i] / 3.0f;
  }
  for (int i = 0; i < 3; i++) out.scale[i] = avg / range[i];
  out.valid = true;
  return true;
}

static Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static float norm(const Vec3& a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }

bool tilt_heading(const Vec3& accel, const Vec3& mag, float& heading_deg) {
  // At rest the accelerometer reads the reaction to gravity, i.e. it points UP.
  Vec3 down = {-accel.x, -accel.y, -accel.z};
  if (norm(down) < 0.3f) return false;  // free fall / garbage
  // NED in board coordinates: East = Down x B, North = East x Down.
  Vec3 east = cross(down, mag);
  float ne = norm(east);
  if (ne < 1e-3f) return false;  // field parallel to gravity (only near the poles)
  Vec3 north = cross(east, down);
  float nn = norm(north);
  // Heading of the board +Y axis: its components along East and North.
  float e = east.y / ne, n = north.y / nn;
  if (fabsf(e) < 1e-4f && fabsf(n) < 1e-4f) return false;  // +Y pointing straight up/down
  float h = atan2f(e, n) * kRad2Deg;
  if (h < 0) h += 360.0f;
  heading_deg = h;
  return true;
}

float AngleSmoother::update(float deg) {
  float r = deg / kRad2Deg;
  float s = sinf(r), c = cosf(r);
  if (!init_) { s_ = s; c_ = c; init_ = true; }
  else { s_ += a_ * (s - s_); c_ += a_ * (c - c_); }
  return value();
}

float AngleSmoother::value() const {
  float h = atan2f(s_, c_) * kRad2Deg;
  return h < 0 ? h + 360.0f : h;
}

bool level_angles(const Vec3& a, float& roll, float& pitch, float& tilt) {
  float n = norm(a);
  if (n < 0.3f) return false;
  roll = atan2f(a.x, sqrtf(a.y * a.y + a.z * a.z)) * kRad2Deg;
  pitch = atan2f(a.y, sqrtf(a.x * a.x + a.z * a.z)) * kRad2Deg;
  float c = a.z / n;
  if (c > 1) c = 1;
  if (c < -1) c = -1;
  tilt = acosf(c) * kRad2Deg;
  return true;
}

const char* cardinal16(float deg) {
  static const char* names[16] = {"N",  "NNE", "NE", "ENE", "E",  "ESE", "SE", "SSE",
                                  "S",  "SSW", "SW", "WSW", "W",  "WNW", "NW", "NNW"};
  deg = fmodf(deg, 360.0f);
  if (deg < 0) deg += 360.0f;
  return names[(int)((deg + 11.25f) / 22.5f) % 16];
}
