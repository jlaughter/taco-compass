// Drawing for the 466x466 round AMOLED. Everything renders into a PSRAM
// framebuffer and is pushed in one go, so there's no flicker.
#pragma once
#include <stdint.h>

enum class Theme : uint8_t { Taco = 0, Pin = 1, Icon = 2 };

struct IconBitmap {          // RGB565 pixels + 1-bit opacity mask, row-major
  int w, h;
  const uint16_t* pix;
  const uint8_t* mask;
};

struct CompassView {
  bool haveTarget = false;
  float arrowDeg = 0;        // where to go, relative to the top of the screen (clockwise)
  float headingDeg = 0;      // which way the top of the screen points (true north = 0)
  bool headingOk = false;
  char headingSrc = '-';     // 'M' magnetometer, 'G' GPS course, '-' none
  float meters = 0;
  const char* name = "";     // place name; empty when it's just the category (e.g. Taco Bell)
  const char* street = "";
  const char* cityState = "";
  int rank = 0;              // 0-based store slot (ignored when isHome)
  int count = 0;             // number of store slots; Home is drawn as one more
  bool isHome = false;       // showing the Home waypoint
  bool homeUnset = false;    // Home slot selected but never saved
  int sats = 0;
  bool liveFix = false;      // false = using the last saved fix
  int battPct = -1;
  bool charging = false;
  bool arrived = false;
  bool magMissing = false;
  bool magUncal = false;
};

struct CompassPage {
  bool headingOk = false;
  float headingDeg = 0;      // true if havePos, else magnetic
  char src = '-';            // 'M' magnetometer, 'G' GPS course
  bool havePos = false;
  bool liveFix = false;
  double lat = 0, lon = 0;
  float declDeg = 0;
  bool altOk = false;
  float altM = 0;
  float speedMps = 0;
  float tiltDeg = 0;
  bool magMissing = false;
  bool magUncal = false;
};

struct LevelPage {
  bool ok = false;           // accelerometer usable and roughly face-up
  float rollDeg = 0;         // + = 3 o'clock edge high
  float pitchDeg = 0;        // + = 12 o'clock edge high
  float tiltDeg = 0;
  bool zeroed = false;       // a level zero is stored (else show how to set one)
};

// About page (also the power-on screen). Text may contain inline icons:
// {heart} {tree} {taco} {star}. Empty lines are skipped; scroll "" = no ticker.
struct AboutPage {
  const char* title = "";   // "" = device name
  const char* line1 = "";
  const char* line2 = "";
  const char* line3 = "";
  const char* scroll = "";
  const char* status = "";  // small grey line, e.g. "8861 Taco Bells"
  bool statusWarn = false;  // show status in amber
};
// Nacho Dog game: what to draw this frame (the game logic lives in game.cpp).
enum class FoodKind : uint8_t { Chip, Taco, Churro, Burger };
struct FoodItem {
  FoodKind kind;
  int16_t x, y;      // screen px, centre
  bool eaten = false;
};
enum class GameState : uint8_t { Idle, Play, Over };
struct NachoView {
  GameState state = GameState::Idle;
  float dogY = 0;              // screen px, centre of the pup
  float dogTiltDeg = 0;        // nose up (+) / down (-)
  uint32_t now = 0;            // for flapping ears and blinking text
  const FoodItem* items = nullptr;
  int itemCount = 0;
  int score = 0, best = 0, level = 1;
  int livesLeft = 3;           // sombreros
  bool newBest = false;
  uint32_t chompUntil = 0;     // mouth open until (ms)
  int popupScore = 0;          // "+10" floating near the pup
  uint32_t popupUntil = 0;
  uint32_t hurtUntil = 0;      // pup blinks after a burger
};

namespace ui {
bool begin(uint8_t brightness);
// Look & feel: theme picks the place icon and accent colors; metric picks units.
// title is the category ("Taco Bell", "Breweries"); deviceName goes on the splash.
// iconL/iconS are only used by Theme::Icon (may be null otherwise).
void setStyle(Theme theme, bool metric, const char* title, const char* deviceName,
              const IconBitmap* iconL, const IconBitmap* iconS);
void sleep();
void wake(uint8_t brightness);
void compass(const CompassView& v, uint32_t nowMs);
void compassPage(const CompassPage& c);
void levelPage(const LevelPage& l);

void aboutPage(const AboutPage& a, uint32_t now);
void nachoPage(const NachoView& v);
void searching(int sats, uint32_t nowMs, bool haveDb);
void calibrating(float progress01, float coverage01, int secondsLeft);
void splash(const char* line1, const char* line2);  // shows deviceName as the headline
void message(const char* title, const char* line1, const char* line2 = "");
void flush();
// Briefly show which page you're on (vertical dots at the right edge) until untilMs.
void pageHint(int page, int count, uint32_t untilMs);
void setBattery(int pct, bool charging);  // drawn on every page; pct < 0 = no battery (hidden)
}
