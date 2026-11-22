// Debounced button with short-press and long-press events.
#pragma once
#include <stdint.h>

enum class BtnEvent : uint8_t { None, Short, Long };

class Button {
 public:
  Button(uint32_t longMs = 2500, uint32_t debounceMs = 30)
      : longMs_(longMs), debMs_(debounceMs) {}
  // Call often with the raw "is pressed" level and a millisecond clock.
  // Short fires on release (if long didn't fire); Long fires while still held.
  BtnEvent update(bool pressedRaw, uint32_t nowMs);
  bool isDown() const { return stable_; }
  // Swallow the rest of the current press (no Short on release, no Long).
  void cancel() { if (stable_) longFired_ = true; }

 private:
  uint32_t longMs_, debMs_;
  bool raw_ = false, stable_ = false, longFired_ = false;
  uint32_t rawSince_ = 0, downSince_ = 0;
};

// Swipe recognizer for the touch panel. Feed it every poll tick:
// touching=true with coordinates while a finger is down, false otherwise.
enum class Swipe : int8_t { None = 0, Left, Right, Up, Down, Tap };  // direction the finger moved (or a tap)
class SwipeDetector {
 public:
  SwipeDetector(int minDx = 70, uint32_t releaseMs = 90, uint32_t maxMs = 1200)
      : minDx_(minDx), releaseMs_(releaseMs), maxMs_(maxMs) {}
  Swipe update(bool touching, int16_t x, int16_t y, uint32_t nowMs);
  bool active() const { return active_; }

 private:
  int minDx_;
  uint32_t releaseMs_, maxMs_;
  bool active_ = false;
  int16_t x0_ = 0, y0_ = 0, xl_ = 0, yl_ = 0;
  uint32_t t0_ = 0, tl_ = 0;
};
