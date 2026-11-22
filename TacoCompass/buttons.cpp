#include "buttons.h"

BtnEvent Button::update(bool pressed, uint32_t now) {
  if (pressed != raw_) { raw_ = pressed; rawSince_ = now; }
  BtnEvent ev = BtnEvent::None;
  if (raw_ != stable_ && (uint32_t)(now - rawSince_) >= debMs_) {
    stable_ = raw_;
    if (stable_) {
      downSince_ = now;
      longFired_ = false;
    } else if (!longFired_) {
      ev = BtnEvent::Short;
    }
  }
  if (stable_ && !longFired_ && (uint32_t)(now - downSince_) >= longMs_) {
    longFired_ = true;
    ev = BtnEvent::Long;
  }
  return ev;
}

Swipe SwipeDetector::update(bool touching, int16_t x, int16_t y, uint32_t now) {
  if (touching) {
    if (!active_) { active_ = true; x0_ = x; y0_ = y; t0_ = now; }
    xl_ = x; yl_ = y; tl_ = now;
    return Swipe::None;
  }
  if (!active_ || (uint32_t)(now - tl_) < releaseMs_) return Swipe::None;
  active_ = false;  // finger lifted
  if ((uint32_t)(tl_ - t0_) > maxMs_) return Swipe::None;  // slow drag
  int dx = xl_ - x0_, dy = yl_ - y0_;
  int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
  // Must be clearly one axis (1.5:1) so diagonals don't do anything surprising.
  if (adx >= minDx_ && adx * 2 >= ady * 3) return dx < 0 ? Swipe::Left : Swipe::Right;
  if (ady >= minDx_ && ady * 2 >= adx * 3) return dy < 0 ? Swipe::Up : Swipe::Down;
  if (adx < 25 && ady < 25 && (uint32_t)(tl_ - t0_) < 500) return Swipe::Tap;  // short, still touch
  return Swipe::None;
}
