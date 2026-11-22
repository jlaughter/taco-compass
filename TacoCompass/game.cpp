#include "game.h"
#include <math.h>

namespace {
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
int points(FoodKind k) {
  switch (k) {
    case FoodKind::Chip: return 5;
    case FoodKind::Taco: return 10;
    case FoodKind::Churro: return 20;
    default: return 0;
  }
}
}  // namespace

uint32_t NachoGame::rnd() {  // xorshift32
  seed_ ^= seed_ << 13;
  seed_ ^= seed_ >> 17;
  seed_ ^= seed_ << 5;
  return seed_;
}

void NachoGame::start(uint32_t now, float tiltDeg, uint32_t seed) {
  state_ = GameState::Play;
  tilt0_ = tiltDeg;
  dogY_ = (TOP + BOTTOM) / 2;
  dogVy_ = dogTilt_ = 0;
  n_ = 0;
  score_ = 0;
  lives_ = LIVES;
  newBest_ = false;
  last_ = now;
  nextSpawn_ = now + 700;
  seed_ = seed ? seed : 0x9E3779B9u;
  chompUntil_ = popupUntil_ = hurtUntil_ = 0;
  popup_ = 0;
}

void NachoGame::end(uint32_t now) {
  if (state_ != GameState::Play) return;
  (void)now;
  state_ = GameState::Over;
  if (score_ > best_) { best_ = score_; newBest_ = true; bestDirty_ = true; }
}

void NachoGame::injectItem(FoodKind k, float x, float y) {
  if (n_ >= MAX_ITEMS) return;
  items_[n_] = {k, (int16_t)x, (int16_t)y, false};
  fx_[n_] = x;
  n_++;
}

void NachoGame::spawn(uint32_t now) {
  int lv = level();
  // Odds: burgers get a little more common each level (20% -> 35%).
  int burger = 20 + 3 * (lv - 1);
  if (burger > 35) burger = 35;
  int r = (int)(rnd() % 100);
  FoodKind k = r < burger ? FoodKind::Burger
             : r < burger + 12 ? FoodKind::Churro
             : r < burger + 40 ? FoodKind::Taco
             : FoodKind::Chip;
  float y = TOP + 8 + (float)(rnd() % (uint32_t)(BOTTOM - TOP - 16));
  injectItem(k, 466 + 30, y);  // starts off the right edge
  uint32_t gap = 1150 - 110 * (uint32_t)(lv - 1);
  if (lv > 8) gap = 380;
  gap += rnd() % 300;  // a little irregular
  nextSpawn_ = now + gap;
}

void NachoGame::eat(FoodItem& f, uint32_t now) {
  f.eaten = true;
  chompUntil_ = now + 200;
  if (f.kind == FoodKind::Burger) {
    lives_--;
    hurtUntil_ = now + 900;
    popup_ = 0;
    if (lives_ <= 0) {
      lives_ = 0;
      end(now);
    }
    return;
  }
  score_ += points(f.kind);
  popup_ = points(f.kind);
  popupUntil_ = now + 700;
}

void NachoGame::update(uint32_t now, float tiltDeg) {
  if (state_ != GameState::Play) return;
  float dt = (now - last_) / 1000.0f;
  last_ = now;
  if (dt > 0.1f) dt = 0.1f;  // after a nap or a hiccup: don't teleport
  if (dt <= 0) return;

  // Pup: tilt sets the target height; ease toward it, nose follows the climb.
  float rel = clampf((tiltDeg - tilt0_) / FULL_TILT_DEG, -1, 1);
  float target = (TOP + BOTTOM) / 2 - rel * (BOTTOM - TOP) / 2;
  float prev = dogY_;
  dogY_ += (target - dogY_) * clampf(dt * 9, 0, 1);
  dogVy_ = (dogY_ - prev) / dt;
  dogTilt_ = clampf(-dogVy_ * 0.06f, -25, 25);

  // Food: roll left, faster each level.
  float speed = 120 + 22 * (level() - 1);
  if (speed > 330) speed = 330;
  const float mouthX = DOG_X + 22, mouthY = dogY_ + 2;
  int w = 0;
  for (int i = 0; i < n_; i++) {
    fx_[i] -= speed * dt;
    FoodItem f = items_[i];
    f.x = (int16_t)lroundf(fx_[i]);
    if (!f.eaten) {
      float dx = fx_[i] - mouthX, dy = f.y - mouthY;
      if (dx * dx + dy * dy < 30.0f * 30.0f) eat(f, now);
    }
    if (fx_[i] > -40) {  // keep it while it's still on screen
      items_[w] = f;
      fx_[w] = fx_[i];
      w++;
    }
  }
  n_ = w;
  if (state_ == GameState::Play && (int32_t)(now - nextSpawn_) >= 0 && n_ < MAX_ITEMS) spawn(now);
}

NachoView NachoGame::view(uint32_t now) const {
  NachoView v;
  v.state = state_;
  v.dogY = dogY_;
  v.dogTiltDeg = dogTilt_;
  v.now = now;
  v.items = items_;
  v.itemCount = n_;
  v.score = score_;
  v.best = best_;
  v.level = level();
  v.livesLeft = lives_;
  v.newBest = newBest_;
  v.chompUntil = chompUntil_;
  v.popupScore = popup_;
  v.popupUntil = popupUntil_;
  v.hurtUntil = hurtUntil_;
  return v;
}
