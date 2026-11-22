// Renders the real ui.cpp into PPM images using a tiny software implementation of the
// Arduino_GFX calls it uses. Lets you eyeball layouts without hardware.
//   make -C test preview   ->  test/preview/*.png
#include <Arduino_GFX_Library.h>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "../TacoCompass/ui.h"
#include "fixture_db.h"  // brewery KMZ with icon, for the generic-theme previews

// ---- runtime stubs
HWCDC Serial;
uint32_t millis() { return 0; }  // pageHint(..., until=1) shows the hint in previews
void delay(uint32_t) {}
size_t HWCDC::write(uint8_t) { return 1; }
int HWCDC::available() { return 0; }
int HWCDC::read() { return -1; }
int HWCDC::peek() { return -1; }
size_t Print::print(const char* s) { size_t n = 0; while (*s) n += write((uint8_t)*s++); return n; }

// ---- framebuffer + raster
static const int W = 466, H = 466;
static std::vector<uint16_t> FB(W * H);

struct TextState { const GFXfont* font = nullptr; int16_t cx = 0, cy = 0; uint16_t color = 0xFFFF; };
static TextState ts;

static void px(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < W && y < H) FB[y * W + x] = c; }
static void hline(int x0, int x1, int y, uint16_t c) { if (x0 > x1) std::swap(x0, x1); for (int x = x0; x <= x1; x++) px(x, y, c); }

void Arduino_GFX::fillScreen(uint16_t c) { std::fill(FB.begin(), FB.end(), c); }
void Arduino_GFX::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (;;) { px(x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
}
void Arduino_GFX::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t c) {
  for (int a = 0; a < 3600; a++) { float t = a * M_PI / 1800; px(lroundf(x0 + r * cosf(t)), lroundf(y0 + r * sinf(t)), c); }
}
void Arduino_GFX::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t c) {
  for (int y = -r; y <= r; y++) { int w = (int)sqrtf(r * r - y * y); hline(x0 - w, x0 + w, y0 + y, c); }
}
void Arduino_GFX::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c) {
  int minx = std::min({x0, x1, x2}), maxx = std::max({x0, x1, x2}), miny = std::min({y0, y1, y2}), maxy = std::max({y0, y1, y2});
  auto edge = [](int ax, int ay, int bx, int by, int px_, int py_) { return (bx - ax) * (py_ - ay) - (by - ay) * (px_ - ax); };
  for (int y = miny; y <= maxy; y++)
    for (int x = minx; x <= maxx; x++) {
      int a = edge(x0, y0, x1, y1, x, y), b = edge(x1, y1, x2, y2, x, y), d = edge(x2, y2, x0, y0, x, y);
      if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) px(x, y, c);
    }
}
void Arduino_GFX::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) { for (int j = 0; j < h; j++) hline(x, x + w - 1, y + j, c); }
void Arduino_GFX::setFont(const GFXfont* f) { ts.font = f; }
void Arduino_GFX::setCursor(int16_t x, int16_t y) { ts.cx = x; ts.cy = y; }
void Arduino_GFX::setTextColor(uint16_t c) { ts.color = c; }
void Arduino_GFX::getTextBounds(const char* s, int16_t x, int16_t y, int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h) {
  int minx = 32767, miny = 32767, maxx = -32768, maxy = -32768;
  const GFXfont* f = ts.font;
  for (; *s; s++) {
    uint8_t ch = *s;
    if (ch < f->first || ch > f->last) continue;
    const GFXglyph& g = f->glyph[ch - f->first];
    if (g.width && g.height) {
      minx = std::min(minx, x + g.xOffset); maxx = std::max(maxx, x + g.xOffset + g.width - 1);
      miny = std::min(miny, y + g.yOffset); maxy = std::max(maxy, y + g.yOffset + g.height - 1);
    }
    x += g.xAdvance;
  }
  if (maxx < minx) { *x1 = x; *y1 = y; *w = *h = 0; return; }
  *x1 = minx; *y1 = miny; *w = maxx - minx + 1; *h = maxy - miny + 1;
}
size_t Arduino_GFX::write(uint8_t ch) {
  const GFXfont* f = ts.font;
  if (ch < f->first || ch > f->last) return 1;
  const GFXglyph& g = f->glyph[ch - f->first];
  const uint8_t* bm = f->bitmap + g.bitmapOffset;
  int bit = 0;
  for (int yy = 0; yy < g.height; yy++)
    for (int xx = 0; xx < g.width; xx++, bit++)
      if (bm[bit >> 3] & (0x80 >> (bit & 7))) px(ts.cx + g.xOffset + xx, ts.cy + g.yOffset + yy, ts.color);
  ts.cx += g.xAdvance;
  return 1;
}
Arduino_ESP32QSPI::Arduino_ESP32QSPI(int8_t, int8_t, int8_t, int8_t, int8_t, int8_t, bool) {}
Arduino_CO5300::Arduino_CO5300(Arduino_DataBus*, int8_t, uint8_t, int16_t, int16_t, uint8_t, uint8_t, uint8_t, uint8_t) {}
bool Arduino_CO5300::begin(int32_t) { return true; }
void Arduino_CO5300::displayOn() {}
void Arduino_CO5300::displayOff() {}
void Arduino_CO5300::setBrightness(uint8_t) {}
Arduino_Canvas::Arduino_Canvas(int16_t, int16_t, Arduino_G*, int16_t, int16_t, uint8_t) {}
bool Arduino_Canvas::begin(int32_t) { return true; }

static const char* outName = "x";
void Arduino_Canvas::flush(bool) {
  char path[128];
  snprintf(path, sizeof(path), "preview/%s.ppm", outName);
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6 %d %d 255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    uint16_t c = FB[i];
    // mask to the round panel
    int x = i % W - W / 2, y = i / W - H / 2;
    bool in = x * x + y * y <= (W / 2) * (W / 2);
    uint8_t rgb[3] = {(uint8_t)(in ? ((c >> 11) & 31) * 255 / 31 : 40),
                      (uint8_t)(in ? ((c >> 5) & 63) * 255 / 63 : 40),
                      (uint8_t)(in ? (c & 31) * 255 / 31 : 40)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static const IconBitmap kL = {DB_ICON_L_W, DB_ICON_L_H, DB_ICON_L_PIX, DB_ICON_L_MASK};
static const IconBitmap kS = {DB_ICON_S_W, DB_ICON_S_H, DB_ICON_S_PIX, DB_ICON_S_MASK};

static void genericSet(const char* prefix, Theme th, bool metric) {
  char nm[64];
  ui::setStyle(th, metric, "Breweries", "Brew Compass", &kL, &kS);
  CompassView v;
  v.haveTarget = true; v.headingOk = true; v.headingDeg = 12; v.arrowDeg = 40; v.meters = 2650;
  v.name = "NoFo Brew Co"; v.street = "123 GA400 North"; v.cityState = "Cumming, GA";
  v.rank = 1; v.count = 5; v.sats = 9; v.liveFix = true; v.battPct = 71;
  snprintf(nm, sizeof(nm), "%s_finder", prefix); outName = strdup(nm); ui::compass(v, 0);
  CompassView a = v; a.arrived = true;
  snprintf(nm, sizeof(nm), "%s_arrived", prefix); outName = strdup(nm); ui::compass(a, 300);
  snprintf(nm, sizeof(nm), "%s_splash", prefix); outName = strdup(nm); ui::splash("Breweries", "214 places");
}

int main() {
  ui::setStyle(Theme::Taco, false, "Taco Bell", "Taco Compass", nullptr, nullptr);
  ui::begin(200);
  CompassView v;
  v.haveTarget = true; v.headingOk = true; v.headingSrc = 'M'; v.headingDeg = 32;
  v.arrowDeg = 0; v.meters = 3862; v.street = "2295 Market Place Blvd"; v.cityState = "Cumming, GA";
  v.rank = 0; v.count = 5; v.sats = 11; v.liveFix = true; v.battPct = 87;
  ui::pageHint(0, 3, 1);
  outName = "compass_ahead_hint"; ui::compass(v, 0);
  ui::pageHint(0, 0, 0);
  {
    AboutPage a;
    a.title = "Taco Compass"; a.line1 = "For Someone"; a.line2 = "Love, Me"; a.line3 = "{tree} 2026";
    a.scroll = "So you can always find a Taco Bell, and find your way home {heart}";
    a.status = "8,861 Taco Bells";
    outName = "about_0"; ui::aboutPage(a, 0);
    outName = "about_1"; ui::aboutPage(a, 9000);
    outName = "about_2"; ui::aboutPage(a, 21500);
    AboutPage d; d.status = "8,861 Taco Bells";
    outName = "about_default"; ui::aboutPage(d, 0);
  }
  {
    NachoView g; g.best = 340; g.now = 0;
    outName = "nacho_idle"; ui::nachoPage(g);
    FoodItem items[] = {{FoodKind::Chip, 300, 150}, {FoodKind::Taco, 380, 250}, {FoodKind::Burger, 250, 300},
                        {FoodKind::Churro, 340, 330}, {FoodKind::Chip, 420, 190}};
    g.state = GameState::Play; g.items = items; g.itemCount = 5; g.dogY = 220; g.dogTiltDeg = 10;
    g.score = 125; g.level = 3; g.livesLeft = 2; g.now = 1200; g.chompUntil = 1300; g.popupScore = 10; g.popupUntil = 1500;
    outName = "nacho_play"; ui::nachoPage(g);
    g.state = GameState::Over; g.livesLeft = 0; g.score = 355; g.newBest = true; g.now = 0;
    outName = "nacho_over"; ui::nachoPage(g);
  }
  ui::setBattery(64, false);
  outName = "compass_ahead"; ui::compass(v, 0);
  ui::setBattery(-1, false);

  v.arrowDeg = 128; v.headingDeg = 250; v.meters = 120; v.rank = 2; v.street = "1050 Peachtree Industrial Blvd"; v.cityState = "Suwanee, GA";
  outName = "compass_feet_right"; ui::compass(v, 0);

  v.arrowDeg = 200; v.meters = 25800; v.rank = 4; v.liveFix = false; v.battPct = 12; v.charging = true;
  v.street = "401 W Main St"; v.cityState = "Tupelo, MS";
  outName = "compass_stale_far"; ui::compass(v, 0);

  CompassView u = v; u.liveFix = true; u.magUncal = true; u.headingOk = false; u.headingSrc = '-';
  u.arrowDeg = 300; u.meters = 1609.344f * 0.4f; u.rank = 1;
  outName = "compass_uncal"; ui::compass(u, 0);

  CompassView a = v; a.arrived = true; a.liveFix = true; a.street = "2295 Market Place Blvd";
  outName = "arrived"; ui::compass(a, 400);

  outName = "searching"; ui::searching(4, 600, true);
  outName = "calibrating"; ui::calibrating(0.55f, 0.7f, 9);
  outName = "message"; ui::splash("Taco Bell", "8031 places");
  CompassView e; e.headingOk = true; e.headingDeg = 0; e.headingSrc = 'M'; e.sats = 7; e.liveFix = true; e.battPct = 60;
  outName = "empty_db"; ui::compass(e, 0);
  CompassView h = v; h.isHome = true; h.liveFix = true; h.charging = false; h.battPct = 64; h.meters = 1609.344f * 27.3f; h.arrowDeg = 20; h.headingDeg = 10;
  outName = "home"; ui::compass(h, 0);
  CompassView hu = h; hu.isHome = false; hu.homeUnset = true; hu.haveTarget = false;
  outName = "home_unset"; ui::compass(hu, 0);
  CompassView ha = h; ha.arrived = true;
  outName = "home_arrived"; ui::compass(ha, 300);
  CompassPage cp; cp.headingOk = true; cp.headingDeg = 247; cp.src = 'M'; cp.havePos = true; cp.liveFix = true;
  cp.lat = 34.20731; cp.lon = -84.14022; cp.declDeg = -5.8f; cp.altOk = true; cp.altM = 359; cp.speedMps = 1.6f;
  ui::pageHint(1, 3, 1);
  ui::setBattery(24, false);
  outName = "page_compass"; ui::compassPage(cp);
  ui::setBattery(-1, false);
  ui::pageHint(0, 0, 0);
  CompassPage cp2; cp2.headingOk = true; cp2.headingDeg = 3; cp2.magUncal = false;
  outName = "page_compass_nofix"; ui::compassPage(cp2);
  LevelPage lp; lp.ok = true; lp.rollDeg = 3.2f; lp.pitchDeg = -1.4f; lp.tiltDeg = 3.5f;
  ui::setBattery(80, true);
  outName = "page_level"; ui::levelPage(lp);
  ui::setBattery(-1, false);
  LevelPage lv; lv.ok = true; lv.rollDeg = 0.2f; lv.pitchDeg = 0.1f; lv.tiltDeg = 0.25f;
  outName = "page_level_ok"; ui::levelPage(lv);
  LevelPage lf; lf.ok = true; lf.tiltDeg = 170;
  outName = "page_level_flat"; ui::levelPage(lf);
  genericSet("brew_icon", Theme::Icon, false);
  genericSet("brew_pin", Theme::Pin, true);
  ui::setStyle(Theme::Taco, true, "Taco Bell", "Taco Compass", nullptr, nullptr);
  CompassView mv = v; mv.arrived = false; mv.liveFix = true; mv.meters = 3862; mv.rank = 0; mv.arrowDeg = 0;
  mv.street = "2295 Market Place Blvd"; mv.cityState = "Cumming, GA";
  outName = "taco_metric"; ui::compass(mv, 0);
  return 0;
}
