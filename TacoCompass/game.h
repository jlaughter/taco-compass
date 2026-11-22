// Nacho Dog: a tilt-steered snack game. Pure logic (no hardware), so it runs in host tests.
//
// Tilt sets the pup's height, zeroed when a game starts. Food rolls in from the right:
// chips +5, tacos +10, churros +20. Burgers cost a sombrero; lose all three and it's over.
// Every 50 points the food comes faster and more often.
#pragma once
#include <stdint.h>
#include "ui.h"

class NachoGame {
 public:
  static const int MAX_ITEMS = 14;
  static const int LIVES = 3;

  // Field geometry, screen px (466 x 466 round panel).
  static constexpr float DOG_X = 233 - 110;
  static constexpr float TOP = 233 - 138, BOTTOM = 233 + 128;   // pup centre range
  static constexpr float FULL_TILT_DEG = 20;                    // this far tips it top to bottom

  void begin(int best) { best_ = best; }
  void start(uint32_t now, float tiltDeg, uint32_t seed);
  // Advance to `now`; tiltDeg is the raw tilt (start() stored the zero).
  void update(uint32_t now, float tiltDeg);
  void end(uint32_t now);  // BOOT pressed: game over (keeps the score)

  GameState state() const { return state_; }
  bool playing() const { return state_ == GameState::Play; }
  int score() const { return score_; }
  int best() const { return best_; }
  bool bestChanged() const { return bestDirty_; }
  void clearBestChanged() { bestDirty_ = false; }

  // What to draw (now = render time).
  NachoView view(uint32_t now) const;

  // Exposed for tests.
  int level() const { return 1 + score_ / 50; }
  int lives() const { return lives_; }
  float dogY() const { return dogY_; }
  int itemCount() const { return n_; }
  const FoodItem& item(int i) const { return items_[i]; }
  void injectItem(FoodKind k, float x, float y);  // tests: place food

 private:
  uint32_t rnd();
  void spawn(uint32_t now);
  void eat(FoodItem& f, uint32_t now);

  GameState state_ = GameState::Idle;
  float tilt0_ = 0;
  float dogY_ = 233, dogVy_ = 0, dogTilt_ = 0;
  float fx_[MAX_ITEMS] = {};  // sub-pixel x
  FoodItem items_[MAX_ITEMS] = {};
  int n_ = 0;
  int score_ = 0, best_ = 0, lives_ = LIVES;
  bool newBest_ = false, bestDirty_ = false;
  uint32_t last_ = 0, nextSpawn_ = 0, seed_ = 1;
  uint32_t chompUntil_ = 0, popupUntil_ = 0, hurtUntil_ = 0;
  int popup_ = 0;
};
