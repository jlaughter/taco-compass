#include "ui.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <math.h>
#include "config.h"
#include "fontbig.h"
#include "fontmed.h"
#include "fontsmall.h"
#include "heading.h"
#include "geo.h"

namespace {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
const uint16_t BLACK = 0x0000, WHITE = 0xFFFF;
// Accent colors are per theme (set in setStyle). Taco theme: purple + magenta
// (any resemblance to a certain fast-food palette is purely delicious).
uint16_t ACCENT = rgb(0x8E, 0x3C, 0xF0);
uint16_t ACCENT_DIM = rgb(0x3A, 0x16, 0x66);
uint16_t ACCENT2 = rgb(0xEC, 0x2E, 0x8E);
uint16_t ACCENT2_DIM = rgb(0x5A, 0x12, 0x38);

Theme theme = Theme::Taco;
bool metric = false;
const char* dbTitle = "";
const char* devName = "Taco Compass";
IconBitmap iconL = {0, 0, nullptr, nullptr}, iconS = {0, 0, nullptr, nullptr};
// Taco
const uint16_t SHELL = rgb(0xF4, 0xB8, 0x42), SHELL_EDGE = rgb(0xB8, 0x7A, 0x1A);
const uint16_t LETTUCE = rgb(0x5C, 0xC8, 0x48), TOMATO = rgb(0xE8, 0x46, 0x3A);
const uint16_t CHEESE = rgb(0xFF, 0xDC, 0x4C), SPECKLE = rgb(0xD0, 0x8A, 0x26);
const uint16_t GREY = rgb(0x70, 0x70, 0x78);
const uint16_t DARK = rgb(0x26, 0x26, 0x2C);
const uint16_t RED = rgb(0xF0, 0x40, 0x40);
const uint16_t AMBER = rgb(0xFF, 0xB0, 0x20);
const uint16_t GREEN = rgb(0x40, 0xD0, 0x70);

const int CX = LCD_W / 2, CY = LCD_H / 2;

Arduino_DataBus* bus = nullptr;
Arduino_CO5300* panel = nullptr;
Arduino_Canvas* fb = nullptr;

inline void polar(float deg, float r, int16_t& x, int16_t& y) {
  float a = deg * (float)M_PI / 180.0f;
  x = (int16_t)lroundf(CX + r * sinf(a));
  y = (int16_t)lroundf(CY - r * cosf(a));
}

int16_t textWidth(const char* s, const GFXfont* f) {
  int16_t x1, y1;
  uint16_t w, h;
  fb->setFont(f);
  fb->getTextBounds(s, 0, 100, &x1, &y1, &w, &h);
  return (int16_t)w;
}

// Centered at baseline y, trimmed with "..." if wider than maxW.
void centered(const char* s, int y, const GFXfont* f, uint16_t color, int maxW = 380) {
  char buf[64];
  strncpy(buf, s, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  size_t n = strlen(buf);
  while (n > 3 && textWidth(buf, f) > maxW) {
    n--;
    buf[n - 3] = '.'; buf[n - 2] = '.'; buf[n - 1] = '.'; buf[n] = 0;
  }
  int16_t x1, y1;
  uint16_t w, h;
  fb->setFont(f);
  fb->getTextBounds(buf, 0, y, &x1, &y1, &w, &h);
  fb->setTextColor(color);
  fb->setCursor(CX - (int)w / 2 - x1, y);
  fb->print(buf);
}

// Text centered on (x, y) both ways.
void textAt(const char* s, int x, int y, const GFXfont* f, uint16_t color) {
  int16_t bx, by;
  uint16_t w, h;
  fb->setFont(f);
  fb->getTextBounds(s, 0, 0, &bx, &by, &w, &h);
  fb->setTextColor(color);
  fb->setCursor(x - (int)w / 2 - bx, y - (int)h / 2 - by);
  fb->print(s);
}

// Centered number with a hand-drawn degree mark (the fonts are plain ASCII).
void drawDegrees(const char* num, int y, const GFXfont* f, uint16_t color, int ringR) {
  int16_t x1, y1;
  uint16_t w, h;
  fb->setFont(f);
  fb->getTextBounds(num, 0, y, &x1, &y1, &w, &h);
  int x0 = CX - (int)w / 2 - x1;
  fb->setTextColor(color);
  fb->setCursor(x0, y);
  fb->print(num);
  int cx = x0 + x1 + w + ringR + 4, cy = y1 + ringR;
  fb->drawCircle(cx, cy, ringR, color);
  fb->drawCircle(cx, cy, ringR - 1, color);
  if (ringR > 6) fb->drawCircle(cx, cy, ringR - 2, color);
}

void dotsRing(float r, int n, int lit, uint16_t on, uint16_t off, int dot) {
  for (int i = 0; i < n; i++) {
    int16_t x, y;
    polar(360.0f * i / n, r, x, y);
    fb->fillCircle(x, y, dot, i < lit ? on : off);
  }
}

void roseRing(float headingDeg) {
  // Bezel ticks every 10 deg, rotating with you; north gets a red wedge.
  for (int d = 0; d < 360; d += 10) {
    float a = d - headingDeg;
    bool major = d % 90 == 0;
    int16_t x0, y0, x1, y1;
    polar(a, 230, x0, y0);
    polar(a, major ? 212 : 222, x1, y1);
    fb->drawLine(x0, y0, x1, y1, major ? GREY : DARK);
  }
  int16_t tx, ty, lx, ly, rx, ry;
  polar(-headingDeg, 210, tx, ty);
  polar(-headingDeg - 4, 231, lx, ly);
  polar(-headingDeg + 4, 231, rx, ry);
  fb->fillTriangle(tx, ty, lx, ly, rx, ry, RED);
}

// Upper half of a disc whose flat edge runs along y.
void halfDiskUp(int x, int y, int r, uint16_t c) {
  for (int dy = 0; dy <= r; dy++) {
    int w = (int)sqrtf((float)(r * r - dy * dy));
    fb->fillRect(x - w, y - dy, 2 * w + 1, 1, c);
  }
}

// A side-view taco lying flat: (x, y) is the middle of its flat bottom edge, r its
// overall radius. Fillings spill out along the curved top. full=false = grey silhouette.
void taco(int x, int y, int r, bool full) {
  int shellR = (int)lroundf(r * 0.84f);
  int n = r < 14 ? 5 : r < 30 ? 7 : 9;
  int bump = (int)fmaxf(2, r * (r < 14 ? 0.24f : 0.17f));
  const float a0 = 18, a1 = 162;  // degrees along the arc, right to left
  auto arc = [&](float deg, float rad, int& px, int& py) {
    float t = deg * (float)M_PI / 180.0f;
    px = x + (int)lroundf(rad * cosf(t));
    py = y - (int)lroundf(rad * sinf(t));
  };
  for (int i = 0; i < n; i++) {  // lettuce frill
    int px, py;
    arc(a0 + (a1 - a0) * i / (n - 1), r * 0.9f, px, py);
    fb->fillCircle(px, py, bump, full ? LETTUCE : rgb(0x48, 0x48, 0x50));
  }
  if (full) {  // tomato and cheese peeking out between the lettuce
    for (int i = 0; i < n - 1; i++) {
      int px, py;
      arc(a0 + (a1 - a0) * (i + 0.5f) / (n - 1), r * 0.95f, px, py);
      fb->fillCircle(px, py, (int)fmaxf(1, bump * 0.7f), (i % 2) ? CHEESE : TOMATO);
    }
  }
  halfDiskUp(x, y, shellR, full ? SHELL_EDGE : GREY);
  halfDiskUp(x, y - 1, shellR - (int)fmaxf(1, r * 0.08f), full ? SHELL : GREY);
  if (full && r >= 20) {  // a few toasty speckles
    static const float sp[6][2] = {{-.45f, .2f}, {.1f, .4f}, {.45f, .18f}, {-.2f, .6f}, {.25f, .55f}, {-.55f, .45f}};
    for (auto& q : sp) fb->fillCircle(x + (int)(q[0] * shellR), y - (int)(q[1] * shellR), (int)fmaxf(1, r * 0.04f), SPECKLE);
  }
}

void pointer(float deg, uint16_t color, uint16_t color2) {
  // A chunky two-tone chevron that orbits the centre and points toward the goal.
  int16_t tx, ty, lx, ly, rx, ry, nx, ny;
  polar(deg, 204, tx, ty);
  polar(deg - 15, 154, lx, ly);
  polar(deg + 15, 154, rx, ry);
  polar(deg, 171, nx, ny);
  fb->fillTriangle(tx, ty, lx, ly, nx, ny, color);
  fb->fillTriangle(tx, ty, nx, ny, rx, ry, color2);
}

// A classic map pin; (x, y) is its middle, r about half its height.
void pin(int x, int y, int r, bool full) {
  int hy = y - (int)(r * 0.3f), hr = (int)lroundf(r * 0.6f);
  uint16_t c = full ? ACCENT : GREY;
  fb->fillTriangle(x - (int)(hr * 0.86f), hy + hr / 2, x + (int)(hr * 0.86f), hy + hr / 2, x, y + r, c);
  fb->fillCircle(x, hy, hr, c);
  fb->fillCircle(x, hy, (int)fmaxf(2, hr * 0.42f), full ? WHITE : BLACK);
}

void bitmap(const IconBitmap& b, int cx, int cy, bool full) {
  int x0 = cx - b.w / 2, y0 = cy - b.h / 2;
  for (int j = 0; j < b.h; j++)
    for (int i = 0; i < b.w; i++) {
      int k = j * b.w + i;
      if (!(b.mask[k >> 3] & (0x80 >> (k & 7)))) continue;
      uint16_t c = b.pix[k];
      if (!full) {  // muted grey version for unselected slots
        int lum = (((c >> 11) & 31) * 8 * 3 + ((c >> 5) & 63) * 4 * 6 + (c & 31) * 8) / 10;
        int g = 50 + lum / 3;
        c = rgb(g, g, g + 6);
      }
      fb->fillRect(x0 + i, y0 + j, 1, 1, c);
    }
}

// The place marker for the current theme, centred on (x, y); r ~ half its size.
void placeIcon(int x, int y, int r, bool full) {
  switch (theme) {
    case Theme::Taco: taco(x, y + (int)(r * 0.45f), r, full); break;
    case Theme::Pin: pin(x, y, (int)(r * 0.72f), full); break;
    case Theme::Icon: bitmap(r >= 45 ? iconL : iconS, x, y, full); break;
  }
}

void house(int x, int y, uint16_t c, int k = 1) {
  fb->fillTriangle(x - 9 * k, y - k, x + 9 * k, y - k, x, y - 10 * k, c);
  fb->fillRect(x - 6 * k, y - k, 13 * k, 9 * k, c);
}

// Store dots followed by a little house for the Home slot.
void slotRow(int count, int selected, int y) {
  int n = count + 1;
  for (int i = 0; i < n; i++) {
    int pitch = theme == Theme::Icon && iconS.w + 6 > 28 ? iconS.w + 6 : 28;
    int x = CX + (int)lroundf((i - (n - 1) / 2.0f) * pitch);
    if (i == count) house(x, y + 2, i == selected ? ACCENT2 : GREY);
    else placeIcon(x, y - 1, i == selected ? 13 : 9, i == selected);
  }
}

void statusBar(int sats, bool live, int batt, bool charging) {
  char s[32];
  if (batt >= 0) snprintf(s, sizeof(s), "%d sat  %d%%%s", sats, batt, charging ? "+" : "");
  else snprintf(s, sizeof(s), "%d sat  USB", sats);
  centered(s, CY - 106, &FontSmall, live ? GREY : AMBER, 200);
}

}  // namespace

namespace ui {

bool begin(uint8_t brightness) {
  bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_SDIO0, PIN_LCD_SDIO1,
                              PIN_LCD_SDIO2, PIN_LCD_SDIO3);
  // Same panel offsets Waveshare uses for this board (col offset 6).
  panel = new Arduino_CO5300(bus, PIN_LCD_RST, 0, LCD_W, LCD_H, 6, 0, 0, 0);
  fb = new Arduino_Canvas(LCD_W, LCD_H, panel);
  if (!fb->begin()) return false;  // framebuffer (~425 KB) lands in PSRAM
#if DISPLAY_FLIP_180
  fb->setRotation(2);  // the canvas rotates as it draws: no per-frame cost
#endif
  fb->fillScreen(BLACK);
  fb->flush();
  panel->setBrightness(brightness);
  return true;
}

void setStyle(Theme t, bool m, const char* title, const char* deviceName, const IconBitmap* l,
              const IconBitmap* sm) {
  theme = (t == Theme::Icon && !(l && sm)) ? Theme::Pin : t;
  metric = m;
  dbTitle = title;
  devName = deviceName;
  if (l) iconL = *l;
  if (sm) iconS = *sm;
  if (theme == Theme::Taco) {
    ACCENT = rgb(0x8E, 0x3C, 0xF0); ACCENT_DIM = rgb(0x3A, 0x16, 0x66);
    ACCENT2 = rgb(0xEC, 0x2E, 0x8E); ACCENT2_DIM = rgb(0x5A, 0x12, 0x38);
  } else {  // calmer teal + blue for everything that isn't tacos
    ACCENT = rgb(0x1F, 0xC8, 0xB0); ACCENT_DIM = rgb(0x0C, 0x4A, 0x42);
    ACCENT2 = rgb(0x3A, 0x8E, 0xF0); ACCENT2_DIM = rgb(0x14, 0x34, 0x5A);
  }
}

void sleep() {
  fb->fillScreen(BLACK);
  fb->flush();
  panel->setBrightness(0);
  panel->displayOff();
}

void wake(uint8_t brightness) {
  panel->displayOn();
  panel->setBrightness(brightness);
}

namespace {
int hintPage = 0, hintCount = 0;
uint32_t hintUntil = 0;
int battPct = -1;
bool battCharging = false;

// Battery gauge: a short arc hugging the bezel at 6 o'clock, filling left to right.
void batteryArc() {
  if (battPct < 0) return;
  const float A0 = 205, A1 = 155;           // left end -> right end (clockwise from 12)
  const float R_IN = 223, R_OUT = 230;
  int pct = battPct > 100 ? 100 : battPct;
  bool low = pct <= 15 && !battCharging;
  if (low && (millis() / 600) % 2) return;  // blink when low
  uint16_t col = battCharging ? ACCENT2 : pct > 30 ? GREEN : pct > 15 ? AMBER : RED;
  float fillTo = A0 + (A1 - A0) * pct / 100.0f;
  // Solid annular segment from angle a to b, as two triangles per 1-degree step.
  auto band = [](float a, float b, float rIn, float rOut, uint16_t c) {
    float step = a > b ? -1.0f : 1.0f;
    for (float t = a; (step < 0 ? t > b : t < b); t += step) {
      float u = (step < 0 ? fmaxf(t + step, b) : fminf(t + step, b));
      int16_t x0, y0, x1, y1, x2, y2, x3, y3;
      polar(t, rIn, x0, y0); polar(t, rOut, x1, y1);
      polar(u, rIn, x2, y2); polar(u, rOut, x3, y3);
      fb->fillTriangle(x0, y0, x1, y1, x3, y3, c);
      fb->fillTriangle(x0, y0, x3, y3, x2, y2, c);
    }
  };
  band(A0 + 2, A1 - 2, R_IN - 3, R_OUT + 3, BLACK);  // backing over bezel ticks
  band(A0, fillTo, R_IN, R_OUT, col);
  band(fillTo, A1, R_IN, R_OUT, DARK);
}
}  // namespace

void setBattery(int pct, bool charging) { battPct = pct; battCharging = charging; }

void pageHint(int page, int count, uint32_t untilMs) {
  hintPage = page; hintCount = count; hintUntil = untilMs;
}

void flush() {
  batteryArc();
  if (hintCount && (int32_t)(millis() - hintUntil) < 0) {
    const int hx = CX + 156;  // between the centre readout and the bezel numerals
    for (int i = 0; i < hintCount; i++) {
      int y = CY + (int)lroundf((i - (hintCount - 1) / 2.0f) * 24);
      fb->fillCircle(hx, y, 11, BLACK);  // dark backing so it reads over anything
    }
    for (int i = 0; i < hintCount; i++) {  // page 0 at the top
      int y = CY + (int)lroundf((i - (hintCount - 1) / 2.0f) * 24);
      if (i == hintPage) fb->fillCircle(hx, y, 7, ACCENT2);
      else fb->drawCircle(hx, y, 5, GREY);
    }
  }
  fb->flush();
}

void compass(const CompassView& v, uint32_t now) {
  fb->fillScreen(BLACK);
  if (v.headingOk) roseRing(v.headingDeg);
  else fb->drawCircle(CX, CY, 220, DARK);

  if (v.arrived) {
    float t = (now % 1200) / 1200.0f;
    for (int k = 0; k < 3; k++) {
      float r = 152 + fmodf(t + k / 3.0f, 1.0f) * 60;
      uint16_t rc = k == 1 ? ACCENT2 : ACCENT;
      fb->drawCircle(CX, CY, (int16_t)r, rc);
      fb->drawCircle(CX, CY, (int16_t)r + 1, rc);
    }
    if (v.isHome) {
      centered("WELCOME", CY - 6, &FontMed, WHITE, 290);
      centered("HOME", CY + 36, &FontMed, WHITE, 290);
    } else {
      placeIcon(CX, CY - 26, 50, true);
      centered("YOU'RE HERE", CY + 50, &FontMed, WHITE, 290);
      centered(v.name[0] ? v.name : v.street, CY + 86, &FontSmall, GREY, 250);
    }
    flush();
    return;
  }

  if (v.haveTarget && v.headingOk) pointer(v.arrowDeg, ACCENT, ACCENT2);
  else if (v.haveTarget) {
    // No heading yet: show bearing as a dimmed pointer relative to north-up.
    pointer(v.arrowDeg, ACCENT_DIM, ACCENT2_DIM);
  }

  statusBar(v.sats, v.liveFix, v.battPct, v.charging);

  if (v.homeUnset) {
    house(CX, CY - 46, ACCENT2, 3);
    centered("Home not set", CY + 4, &FontMed, WHITE, 260);
    centered("hold PWR + BOOT", CY + 40, &FontSmall, GREY, 260);
    centered("while you're home", CY + 66, &FontSmall, GREY, 300);
    slotRow(v.count, v.count, CY + 126);
    flush();
    return;
  }

  if (!v.haveTarget) {
    centered("No places loaded", CY - 6, &FontMed, WHITE, 300);
    centered("run tools/build_db.py", CY + 30, &FontSmall, GREY, 320);
    slotRow(0, -1, CY + 126);
    flush();
    return;
  }

  // Everything below stays inside a r~145 disc so the orbiting pointer never covers it.
  char num[16], unit[12];
  format_distance(v.meters, metric, num, sizeof(num), unit, sizeof(unit));
  centered(num, CY + 20, &FontBig, WHITE, 270);
  centered(unit, CY + 48, &FontSmall, GREY);
  if (v.isHome) {
    centered("HOME", CY + 82, &FontMed, WHITE, 240);
  } else {
    // Named places (breweries...): name + street. Chains (all "Taco Bell"): street + city.
    const char* l1 = v.name;
    const char* l2 = v.street[0] ? v.street : v.cityState;
    if (!l1[0]) { l1 = v.street; l2 = v.cityState; }
    if (!l1[0]) { l1 = v.cityState[0] ? v.cityState : dbTitle; l2 = ""; }
    centered(l1, CY + 78, &FontSmall, WHITE, 240);
    centered(l2, CY + 104, &FontSmall, GREY, 220);
  }
  slotRow(v.count, v.isHome ? v.count : v.rank, CY + 126);

  if (v.magMissing) centered("no magnetometer", CY - 76, &FontSmall, RED, 230);
  else if (v.magUncal) centered("needs calibration", CY - 76, &FontSmall, AMBER, 230);
  else if (!v.liveFix) centered("last known fix", CY - 76, &FontSmall, AMBER, 230);
  flush();
}

void compassPage(const CompassPage& c) {
  fb->fillScreen(BLACK);
  float h = c.headingOk ? c.headingDeg : 0;
  // Bezel: 5 deg minor, 10 deg medium, 30 deg major ticks, rotating with you.
  for (int d = 0; d < 360; d += 5) {
    float a = d - h;
    int inner = d % 30 == 0 ? 206 : d % 10 == 0 ? 216 : 223;
    int16_t x0, y0, x1, y1;
    polar(a, 231, x0, y0);
    polar(a, inner, x1, y1);
    uint16_t col = d % 30 == 0 ? WHITE : d % 10 == 0 ? GREY : DARK;
    fb->drawLine(x0, y0, x1, y1, col);
    if (d % 30 == 0) {  // thicken majors
      int16_t x2, y2, x3, y3;
      polar(a + 0.4f, 231, x2, y2);
      polar(a + 0.4f, inner, x3, y3);
      fb->drawLine(x2, y2, x3, y3, col);
    }
  }
  static const char* card[4] = {"N", "E", "S", "W"};
  for (int i = 0; i < 12; i++) {
    int d = i * 30;
    int16_t x, y;
    if (d % 90 == 0) {
      polar(d - h, 184, x, y);
      textAt(card[d / 90], x, y, &FontMed, d == 0 ? RED : WHITE);
    } else {
      char n[4];
      snprintf(n, sizeof(n), "%d", d);
      polar(d - h, 186, x, y);
      textAt(n, x, y, &FontSmall, GREY);
    }
  }
  // Fixed lubber line: the direction the top of the device points.
  fb->fillTriangle(CX, CY - 204, CX - 13, CY - 232, CX + 13, CY - 232, ACCENT);

  // Top line: what kind of north, or a warning.
  char line[40];
  if (c.magMissing) centered("no magnetometer", CY - 96, &FontSmall, RED, 230);
  else if (c.magUncal) centered("needs calibration", CY - 96, &FontSmall, AMBER, 230);
  else if (c.tiltDeg > 40) centered("hold it flatter", CY - 96, &FontSmall, AMBER, 230);
  else if (c.src == 'G') centered("GPS COURSE", CY - 96, &FontSmall, GREY, 230);
  else if (c.havePos) {
    snprintf(line, sizeof(line), "TRUE  decl %+.1f", c.declDeg);
    centered(line, CY - 96, &FontSmall, GREY, 230);
  } else centered("MAGNETIC", CY - 96, &FontSmall, GREY, 230);

  if (c.headingOk) {
    char num[8];
    snprintf(num, sizeof(num), "%d", (int)lroundf(c.headingDeg) % 360);
    drawDegrees(num, CY + 14, &FontBig, WHITE, 8);
    centered(cardinal16(c.headingDeg), CY + 58, &FontMed, ACCENT, 200);
  } else {
    centered("---", CY + 14, &FontMed, GREY, 200);
  }

  if (c.havePos) {
    snprintf(line, sizeof(line), "%.4f%c %.4f%c", fabs(c.lat), c.lat >= 0 ? 'N' : 'S', fabs(c.lon),
             c.lon >= 0 ? 'E' : 'W');
    centered(line, CY + 94, &FontSmall, c.liveFix ? WHITE : AMBER, 300);
    line[0] = 0;
    int n = 0;
    if (!metric) {
      if (c.altOk) n += snprintf(line + n, sizeof(line) - n, "%ld ft", lroundf(c.altM * 3.28084f));
      if (c.speedMps > 1.0f)
        snprintf(line + n, sizeof(line) - n, "%s%ld mph", n ? "   " : "", lroundf(c.speedMps * 2.23694f));
    } else {
      if (c.altOk) n += snprintf(line + n, sizeof(line) - n, "%ld m", lroundf(c.altM));
      if (c.speedMps > 1.0f)
        snprintf(line + n, sizeof(line) - n, "%s%ld km/h", n ? "   " : "", lroundf(c.speedMps * 3.6f));
    }
    if (!c.liveFix) snprintf(line, sizeof(line), "last known fix");
    centered(line, CY + 120, &FontSmall, c.liveFix ? GREY : AMBER, 220);
  }
  flush();
}

void levelPage(const LevelPage& l) {
  fb->fillScreen(BLACK);
  const float RMAX = 176, DEGMAX = 20;
  auto radiusFor = [&](float deg) { return RMAX * sqrtf(fminf(deg, DEGMAX) / DEGMAX); };
  // Rings at 5, 10, 20 degrees (square-root scale: sensitive near level).
  const float rings[4] = {0, 5, 10, 20};
  for (int i = 1; i < 4; i++) fb->drawCircle(CX, CY, (int16_t)radiusFor(rings[i]), DARK);
  fb->drawLine(CX - (int)RMAX, CY, CX + (int)RMAX, CY, DARK);
  fb->drawLine(CX, CY - (int)RMAX, CX, CY + (int)RMAX, DARK);
  for (int i = 1; i < 4; i++) {
    char n[4];
    snprintf(n, sizeof(n), "%d", (int)rings[i]);
    int16_t x, y;
    polar(45, radiusFor(rings[i]) + 2, x, y);
    textAt(n, x + 8, y - 8, &FontSmall, GREY);
  }

  if (!l.ok || l.tiltDeg > 45) {
    centered("Lay it flat", CY - 44, &FontMed, AMBER, 320);
    centered("Screen up", CY, &FontMed, AMBER, 320);
    centered("Press & hold BOOT", CY + 44, &FontMed, AMBER, 420);
    flush();
    return;
  }

  // The bubble floats toward the high side.
  float bx = l.rollDeg, by = -l.pitchDeg;
  float t = sqrtf(bx * bx + by * by);
  float r = radiusFor(t);
  int16_t px = CX, py = CY;
  if (t > 1e-3f) {
    px = (int16_t)lroundf(CX + r * bx / t);
    py = (int16_t)lroundf(CY + r * by / t);
  }
  bool level = t < 0.5f;
  uint16_t col = level ? GREEN : t < 2.0f ? AMBER : ACCENT;
  const int BUB = 24;
  // Target ring: the bubble fits inside it exactly when within 0.5 deg of level.
  int16_t tr = (int16_t)(radiusFor(0.5f) + BUB + 2);
  fb->drawCircle(CX, CY, tr, WHITE);
  fb->drawCircle(CX, CY, tr + 1, WHITE);
  fb->fillCircle(px, py, BUB, col);
  fb->fillCircle(px - 7, py - 8, 5, WHITE);  // highlight

  char num[12];
  snprintf(num, sizeof(num), "%.1f", t);
  drawDegrees(num, CY + 214 - 20, &FontMed, level ? GREEN : WHITE, 5);
  char axes[32];
  snprintf(axes, sizeof(axes), "X %+.1f   Y %+.1f", l.rollDeg, l.pitchDeg);
  centered(axes, CY - 192, &FontSmall, GREY, 260);
  if (level) centered("LEVEL", CY - 192 + 30, &FontSmall, GREEN, 200);
  if (!l.zeroed) centered("hold BOOT to zero", CY + 140, &FontSmall, GREY, 260);
  flush();
}

void searching(int sats, uint32_t now, bool haveDb) {
  fb->fillScreen(BLACK);
  int lit = (now / 120) % 24;
  for (int i = 0; i < 24; i++) {
    int16_t x, y;
    polar(i * 15.0f, 200, x, y);
    int age = (lit - i + 24) % 24;
    uint16_t c = age == 0 ? ACCENT2 : age < 3 ? ACCENT : age < 6 ? ACCENT_DIM : DARK;
    fb->fillCircle(x, y, age == 0 ? 7 : 5, c);
  }
  placeIcon(CX, CY - 70, 48, true);
  centered("Looking up", CY + 2, &FontMed, WHITE);
  centered("for satellites", CY + 40, &FontMed, WHITE);
  char s[24];
  snprintf(s, sizeof(s), "%d in view", sats);
  centered(s, CY + 82, &FontSmall, GREY);
  if (!haveDb) centered("(database empty)", CY + 112, &FontSmall, RED);
  flush();
}

void calibrating(float p, float cov, int left) {
  fb->fillScreen(BLACK);
  dotsRing(205, 60, (int)(p * 60), ACCENT, DARK, 5);
  dotsRing(182, 40, (int)(cov * 40), GREEN, DARK, 3);
  centered("Calibrating", CY - 40, &FontMed, WHITE);
  centered("Tumble it in big", CY + 2, &FontSmall, GREY);
  centered("figure-8s, all angles", CY + 30, &FontSmall, GREY);
  char s[16];
  snprintf(s, sizeof(s), "%d", left);
  centered(s, CY + 84, &FontMed, cov > 0.85f ? GREEN : AMBER);
  flush();
}

// ---------------------------------------------------------------- About page
namespace {

// Small vector icons for inline text, sitting on the baseline, s px tall.
void iconHeart(int cx, int base, int s, uint16_t c) {
  int r = s / 4 + 1, top = base - s;
  fb->fillCircle(cx - r + 1, top + r, r, c);
  fb->fillCircle(cx + r - 1, top + r, r, c);
  fb->fillTriangle(cx - 2 * r + 1, top + r + 1, cx + 2 * r - 1, top + r + 1, cx, base, c);
}
void iconStar(int cx, int cy, int r, uint16_t c) {
  for (int k = 0; k < 5; k++) {  // five fat triangles from the centre
    int16_t x0, y0, x1, y1, x2, y2;
    float a = k * 72.0f;
    x0 = cx + (int)lroundf(r * sinf(a * DEG_TO_RAD));
    y0 = cy - (int)lroundf(r * cosf(a * DEG_TO_RAD));
    x1 = cx + (int)lroundf(r * 0.42f * sinf((a + 36) * DEG_TO_RAD));
    y1 = cy - (int)lroundf(r * 0.42f * cosf((a + 36) * DEG_TO_RAD));
    x2 = cx + (int)lroundf(r * 0.42f * sinf((a - 36) * DEG_TO_RAD));
    y2 = cy - (int)lroundf(r * 0.42f * cosf((a - 36) * DEG_TO_RAD));
    fb->fillTriangle(x0, y0, x1, y1, cx, cy, c);
    fb->fillTriangle(x0, y0, x2, y2, cx, cy, c);
  }
}
void iconTree(int cx, int base, int s) {
  const uint16_t PINE = rgb(0x2E, 0xA8, 0x4C), BARK = rgb(0x8A, 0x55, 0x2A);
  int w = s * 3 / 4, trunk = s / 7 + 1;
  fb->fillRect(cx - trunk / 2 - 1, base - trunk - 1, trunk + 2, trunk + 1, BARK);
  int bottom = base - trunk;
  int tiers = 3, tierH = (bottom - (base - s) - 2) / 2;
  for (int t = 0; t < tiers; t++) {
    int y1 = bottom - t * tierH * 2 / 3;
    int hw = (w / 2) * (tiers - t) / tiers + 2;
    fb->fillTriangle(cx - hw, y1, cx + hw, y1, cx, y1 - tierH, PINE);
  }
  fb->fillCircle(cx - w / 5, bottom - tierH / 2, 1, RED);
  fb->fillCircle(cx + w / 6, bottom - tierH, 1, CHEESE);
  fb->fillCircle(cx - 1, bottom - tierH * 3 / 2, 1, RED);
  iconStar(cx, base - s + 1, s / 6 + 2, CHEESE);
}

enum class Ico : uint8_t { None, Heart, Tree, Taco, Star };
Ico iconNamed(const char* n, size_t len) {
  auto is = [&](const char* k) { return strlen(k) == len && !strncmp(n, k, len); };
  if (is("heart")) return Ico::Heart;
  if (is("tree")) return Ico::Tree;
  if (is("taco")) return Ico::Taco;
  if (is("star")) return Ico::Star;
  return Ico::None;
}

int advance(char ch, const GFXfont* f) {
  if (ch < f->first || ch > f->last) return 0;
  return f->glyph[ch - f->first].xAdvance;
}

// Walk "text {icon} text": measure (draw=false) or draw at baseline y from x.
int richRun(const char* s, int x, int y, const GFXfont* f, uint16_t color, bool draw) {
  int iconPx = f->yAdvance * 4 / 5;
  int x0 = x;
  fb->setFont(f);
  fb->setTextColor(color);
  for (const char* p = s; *p;) {
    if (*p == '{') {
      const char* e = strchr(p, '}');
      Ico ic = e ? iconNamed(p + 1, e - p - 1) : Ico::None;
      if (ic != Ico::None) {
        int w = iconPx + 2, cx = x + w / 2;
        if (draw) {
          switch (ic) {
            case Ico::Heart: iconHeart(cx, y, iconPx - 2, RED); break;
            case Ico::Tree: iconTree(cx, y + 1, iconPx + 4); break;
            case Ico::Taco: taco(cx, y - iconPx / 3, iconPx / 2 + 2, true); break;
            case Ico::Star: iconStar(cx, y - iconPx / 2, iconPx / 2, CHEESE); break;
            default: break;
          }
        }
        x += w;
        p = e + 1;
        continue;
      }
    }
    if (draw) { fb->setCursor(x, y); fb->write((uint8_t)*p); }
    x += advance(*p, f);
    p++;
  }
  return x - x0;
}

void richCentered(const char* s, int y, const GFXfont* f, uint16_t color) {
  if (!s || !*s) return;
  int w = richRun(s, 0, y, f, color, false);
  richRun(s, CX - w / 2, y, f, color, true);
}

void aboutRing() {
  for (int i = 0; i < 72; i++) {  // the splash's ring of alternating dots
    int16_t x, y;
    polar(i * 5.0f, 214, x, y);
    fb->fillCircle(x, y, 3, i % 2 ? ACCENT : ACCENT2);
  }
}
}  // namespace

void aboutPage(const AboutPage& a, uint32_t now) {
  fb->fillScreen(BLACK);
  const char* lines[3] = {a.line1, a.line2, a.line3};
  int nLines = 0;
  for (auto l : lines) nLines += (l && *l) ? 1 : 0;
  bool ticker = a.scroll && *a.scroll;
  // Stack: icon, title, lines; shift up a bit when there is more to show.
  int top = CY - 58 - nLines * 14 - (ticker ? 22 : 0);
  placeIcon(CX, top, nLines ? 52 : 70, true);
  int y = top + (nLines ? 82 : 100);
  centered(*a.title ? a.title : devName, y, &FontMed, WHITE, 330);
  y += 8;
  bool first = true;
  for (auto l : lines) {
    if (!l || !*l) continue;
    y += first ? 40 : 34;
    richCentered(l, y, first ? &FontMed : &FontSmall, first ? ACCENT2 : WHITE);
    first = false;
  }
  if (ticker) {
    const int ty = CY + 132, half = 150;  // ticker window: inside the ring
    int w = richRun(a.scroll, 0, ty, &FontSmall, WHITE, false);
    const int gap = 90;
    int period = w + gap;
    int off = (int)((now / 25) % (uint32_t)period);  // 40 px/s
    for (int x = CX + half - off; x < CX + half; x += period) richRun(a.scroll, x, ty, &FontSmall, WHITE, true);
    // mask everything outside the window, then the ring goes on top
    fb->fillRect(0, ty - 30, CX - half, 40, BLACK);
    fb->fillRect(CX + half, ty - 30, LCD_W - (CX + half), 40, BLACK);
  }
  if (a.status && *a.status)
    centered(a.status, ticker ? CY + 172 : CY + 160, &FontSmall, a.statusWarn ? AMBER : GREY, 240);
  aboutRing();
  flush();
}

// ---------------------------------------------------------------- Nacho Dog
namespace {
const uint16_t TAN = rgb(0xD8, 0xA2, 0x6A), TAN_D = rgb(0xA8, 0x6E, 0x3C), TAN_L = rgb(0xF2, 0xCC, 0x9C);
const uint16_t EAR_PINK = rgb(0xF2, 0x9C, 0xA8), STRAW = rgb(0xF2, 0xC9, 0x4C), STRAW_D = rgb(0xC8, 0x96, 0x2A);
const uint16_t CHURRO = rgb(0xC8, 0x80, 0x38), CHURRO_D = rgb(0x8E, 0x52, 0x1E), SUGAR = rgb(0xFF, 0xF4, 0xE0);
const uint16_t BUN = rgb(0xE0, 0x96, 0x3C), BUN_D = rgb(0xB0, 0x6A, 0x22), PATTY = rgb(0x6A, 0x36, 0x1C);

void fillEllipseH(float cx, float cy, float rx, float ry, uint16_t c) {
  int r = (int)ceilf(ry);
  for (int dy = -r; dy <= r; dy++) {
    float t = dy / ry;
    if (t * t > 1) continue;
    float half = rx * sqrtf(1 - t * t);
    fb->fillRect((int)lroundf(cx - half), (int)lroundf(cy) + dy, (int)lroundf(2 * half) + 1, 1, c);
  }
}
// Top half of an ellipse (a bun, a sombrero crown).
void fillDome(float cx, float cy, float rx, float ry, uint16_t c) {
  for (int dy = -(int)ceilf(ry); dy <= 0; dy++) {
    float t = dy / ry;
    if (t * t > 1) continue;
    float half = rx * sqrtf(1 - t * t);
    fb->fillRect((int)lroundf(cx - half), (int)lroundf(cy) + dy, (int)lroundf(2 * half) + 1, 1, c);
  }
}

// A rotated point around (cx, cy).
struct P { float x, y; };
P rot(float cx, float cy, float x, float y, float deg) {
  float r = deg * DEG_TO_RAD, s = sinf(r), c = cosf(r);
  float dx = x - cx, dy = y - cy;
  return {cx + dx * c - dy * s, cy + dx * s + dy * c};
}
void tri(P a, P b, P c, uint16_t col) {
  fb->fillTriangle((int)lroundf(a.x), (int)lroundf(a.y), (int)lroundf(b.x), (int)lroundf(b.y),
                   (int)lroundf(c.x), (int)lroundf(c.y), col);
}
void disc(P p, float r, uint16_t col) { fb->fillCircle((int)lroundf(p.x), (int)lroundf(p.y), (int)lroundf(r), col); }

void sombrero(float cx, float cy, float k, uint16_t straw, uint16_t band) {
  fillEllipseH(cx, cy, 15 * k, 3.6f * k, straw);
  fillDome(cx, cy - 1 * k, 7 * k, 9 * k, straw);
  fb->fillRect((int)lroundf(cx - 6.5f * k), (int)lroundf(cy - 3.5f * k), (int)lroundf(13 * k) + 1,
               (int)fmaxf(2, 2.2f * k), band);
  fillEllipseH(cx, cy + 0.6f * k, 15 * k, 1.2f * k, straw == GREY ? DARK : STRAW_D);
}

// The pup, facing right, centred on (x, y). k = scale (1 = ~60 px across).
// earPhase 0..1 flaps the ears (they're the wings); tilt rotates nose up/down.
void pup(float x, float y, float k, float earPhase, float tilt, bool chomp) {
  auto R = [&](float px, float py) { return rot(x, y, x + px * k, y + py * k, -tilt); };
  float flap = sinf(earPhase * 2 * (float)M_PI) * 7;  // ear tip swing, px at k=1 (the ears are the wings)
  // tail: a curl of dots
  for (float t = 0; t <= 1.0f; t += 0.08f) {
    float a = t * 2.2f;  // radians around the curl
    disc(R(-15 - 9 * sinf(a), 2 - 12 * t - 3 * cosf(a) + 3), 2.0f * k, TAN_D);
  }
  // back ear
  tri(R(0, -11), R(9, -15), R(-4 - flap * 0.7f, -36 + flap), TAN_D);
  tri(R(2, -12), R(7, -14), R(-2 - flap * 0.7f, -31 + flap * 0.9f), rgb(0xC8, 0x7A, 0x84));
  // body (small) + legs + chest
  for (float t = 0; t <= 1.0f; t += 0.1f) disc(R(-13 + 15 * t, 6), 8.5f * k, TAN);
  P l1 = R(-12, 12), l2 = R(-1, 12);
  fb->fillRect((int)lroundf(l1.x), (int)lroundf(l1.y), (int)lroundf(3.5f * k), (int)lroundf(7 * k), TAN_D);
  fb->fillRect((int)lroundf(l2.x), (int)lroundf(l2.y), (int)lroundf(3.5f * k), (int)lroundf(7 * k), TAN_D);
  disc(R(-2, 9), 4.5f * k, TAN_L);
  // head: big apple head
  disc(R(9, -4), 13.5f * k, TAN);
  // snout, nose, mouth
  disc(R(19, 3), 5.5f * k, TAN_L);
  disc(R(23.5f, 1), 2.6f * k, TAN_L);
  disc(R(25, 0), 2.3f * k, BLACK);
  if (chomp) tri(R(17, 6), R(26, 4), R(20, 12), rgb(0x60, 0x18, 0x20));
  else {
    P m0 = R(18, 7), m1 = R(24, 5);
    fb->drawLine((int)lroundf(m0.x), (int)lroundf(m0.y), (int)lroundf(m1.x), (int)lroundf(m1.y), TAN_D);
  }
  // eye: big and shiny
  disc(R(13, -6), 4.8f * k, BLACK);
  disc(R(14.6f, -7.8f), 1.6f * k, WHITE);
  // tiny sombrero on top of the head
  P s = R(7, -16);
  sombrero(s.x, s.y, 0.66f * k, STRAW, ACCENT2);
  // front ear, in front of the hat brim
  tri(R(11, -14), R(21, -9), R(18 - flap * 0.5f, -37 + flap), TAN);
  tri(R(13, -13), R(19, -10), R(17.5f - flap * 0.5f, -32 + flap * 0.9f), EAR_PINK);
}

void chip(float x, float y, float spin) {
  const uint16_t TORT = rgb(0xEC, 0xB0, 0x48), TORT_D = rgb(0xC0, 0x82, 0x26);
  P a = rot(x, y, x, y - 15, spin), b = rot(x, y, x - 14, y + 11, spin), c = rot(x, y, x + 14, y + 11, spin);
  tri(a, b, c, TORT_D);
  tri(rot(x, y, x, y - 12, spin), rot(x, y, x - 11, y + 9, spin), rot(x, y, x + 11, y + 9, spin), TORT);
  // nacho cheese blob
  disc(rot(x, y, x + 1, y + 1, spin), 5, CHEESE);
  disc(rot(x, y, x + 4, y + 5, spin), 2, CHEESE);
  disc(rot(x, y, x - 5, y + 5, spin), 1, SPECKLE);
  disc(rot(x, y, x - 1, y - 7, spin), 1, SPECKLE);
}

void churro(float x, float y, float spin) {
  P a = rot(x, y, x - 21, y, 25 + spin), b = rot(x, y, x + 21, y, 25 + spin);
  for (float t = 0; t <= 1.0f; t += 0.05f) disc({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}, 6, CHURRO);
  for (float t = 0.08f; t < 1.0f; t += 0.14f) {  // ridges + sugar
    P p = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    disc({p.x, p.y + 1}, 1, CHURRO_D);
    disc({p.x + 2, p.y - 2}, 1, SUGAR);
  }
}

void burger(float x, float y) {
  fillDome(x, y - 3, 14, 10, BUN);                       // top bun
  disc({x - 6, y - 8}, 1, SUGAR); disc({x + 1, y - 10}, 1, SUGAR); disc({x + 7, y - 7}, 1, SUGAR);
  fb->fillRect((int)x - 15, (int)y - 3, 30, 3, LETTUCE); // lettuce
  fb->fillRect((int)x - 14, (int)y, 28, 3, CHEESE);      // cheese + drips
  tri({x - 9, y + 3}, {x - 5, y + 3}, {x - 7, y + 7}, CHEESE);
  tri({x + 5, y + 3}, {x + 9, y + 3}, {x + 7, y + 6}, CHEESE);
  fb->fillRect((int)x - 14, (int)y + 3, 28, 5, PATTY);   // patty
  fillEllipseH(x, y + 10, 14, 3, BUN_D);                 // bottom bun
}

void foodSprite(const FoodItem& f, uint32_t now) {
  switch (f.kind) {
    // Chips rock gently around a tilted pose, so they never sit point-up like a warning sign.
    case FoodKind::Chip: chip(f.x, f.y, 28 + sinf(now / 380.0f + f.y) * 14); break;
    case FoodKind::Taco: taco(f.x, f.y + 6, 19, true); break;
    case FoodKind::Churro: churro(f.x, f.y, sinf(now / 300.0f) * 12); break;
    case FoodKind::Burger: burger(f.x, f.y); break;
  }
}

void livesRow(int left) {
  for (int i = 0; i < 3; i++) {
    float x = CX - 44 + i * 44, y = CY + 186;
    bool alive = i < left;
    sombrero(x, y, 1.15f, alive ? STRAW : GREY, alive ? ACCENT2 : DARK);
    if (!alive) {
      for (int w = -1; w <= 1; w++) {
        fb->drawLine(x - 14 + w, y - 14, x + 14 + w, y + 6, RED);
        fb->drawLine(x + 14 + w, y - 14, x - 14 + w, y + 6, RED);
      }
    }
  }
}
}  // namespace

void nachoPage(const NachoView& v) {
  fb->fillScreen(BLACK);
  // faint ground speckle that scrolls (speed sense)
  int scroll = (int)((v.now / 12) % 40);
  for (int x = -scroll; x < LCD_W; x += 40) fb->fillCircle(x, CY + 150, 1, DARK);
  float earPhase = (v.now % 380) / 380.0f;

  if (v.state == GameState::Idle) {
    centered("Nacho Dog", CY - 120, &FontMed, ACCENT2, 300);
    pup(CX, CY - 8 + sinf(v.now / 350.0f) * 6, 2.2f, earPhase, 6, false);
    if ((v.now / 600) % 2 == 0) centered("Tap to start", CY + 112, &FontSmall, WHITE, 280);
    char b[32];
    snprintf(b, sizeof(b), "Best %d", v.best);
    centered(b, CY + 150, &FontSmall, GREY, 260);
    flush();
    return;
  }

  if (v.state == GameState::Play) {
    for (int i = 0; i < v.itemCount; i++)
      if (!v.items[i].eaten) foodSprite(v.items[i], v.now);
    bool blinkOff = v.now < v.hurtUntil && (v.now / 110) % 2;
    if (!blinkOff) pup(CX - 110, v.dogY, 1.35f, earPhase, v.dogTiltDeg, v.now < v.chompUntil);
  }
  if (v.state == GameState::Play && v.now < v.popupUntil && v.popupScore) {
    char p[8];
    snprintf(p, sizeof(p), v.popupScore > 0 ? "+%d" : "%d", v.popupScore);
    fb->setFont(&FontSmall);
    fb->setTextColor(v.popupScore > 0 ? CHEESE : RED);
    fb->setCursor(CX - 92, (int)v.dogY - 38 - (int)((v.popupUntil - v.now) < 600 ? (600 - (v.popupUntil - v.now)) / 30 : 0));
    fb->print(p);
  }
  char s[16];
  snprintf(s, sizeof(s), "%d", v.score);
  centered(s, CY - 168, &FontMed, WHITE, 200);
  livesRow(v.livesLeft);

  if (v.state == GameState::Over) {
    pup(CX, CY - 72, 1.5f, 0.25f, -8, false);  // a resting pup
    centered("Game over", CY - 14, &FontMed, ACCENT2, 300);
    snprintf(s, sizeof(s), "%d", v.score);
    char b[32];
    if (v.newBest) centered("NEW BEST!", CY + 30, &FontSmall, CHEESE, 260);
    else { snprintf(b, sizeof(b), "Best %d", v.best); centered(b, CY + 30, &FontSmall, GREY, 260); }
    if ((v.now / 600) % 2 == 0) centered("Tap to play again", CY + 110, &FontSmall, WHITE, 300);
  }
  flush();
}

void splash(const char* line1, const char* line2) {
  fb->fillScreen(BLACK);
  for (int i = 0; i < 72; i++) {  // a ring of alternating dots
    int16_t x, y;
    polar(i * 5.0f, 214, x, y);
    fb->fillCircle(x, y, 3, i % 2 ? ACCENT : ACCENT2);
  }
  placeIcon(CX, CY - 29, 70, true);
  centered(devName, CY + 58, &FontMed, WHITE, 330);
  centered(line1, CY + 98, &FontSmall, GREY, 300);
  centered(line2, CY + 126, &FontSmall, GREY, 260);
  flush();
}

void message(const char* title, const char* l1, const char* l2) {
  fb->fillScreen(BLACK);
  fb->drawCircle(CX, CY, 220, ACCENT_DIM);
  centered(title, CY - 20, &FontMed, WHITE);
  centered(l1, CY + 20, &FontSmall, GREY, 330);
  centered(l2, CY + 48, &FontSmall, GREY, 330);
  flush();
}

}  // namespace ui
