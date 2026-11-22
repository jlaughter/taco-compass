// TacoCompass — points at the nearest Taco Bell.
// Hardware: Waveshare ESP32-S3-Touch-AMOLED-1.75-G + external I2C magnetometer.
//
// Pages: Taco Bell finder -> Compass -> Bubble level  (BOOT short press cycles them)
//
// Controls
//   Swipe left / right : next / previous target (finder page: 5 nearest, then Home)
//   Swipe up / down    : next / previous page
//   BOOT  short : next page (backup for gloves / wet glass)               BOOT long (2.5 s)     : calibrate magnetometer
//                                          (on the Level page: zero the level instead)
//   PWR   short : nap / wake               PWR  hold 6 s         : hard power off (AXP2101)
//   PWR + BOOT held together 1.5 s         : set Home to where you're standing
//   (any button wakes it from a nap; PWR turns it on when it's off)
//
// Serial (USB CDC, 115200): type ? for the command list

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

#include "config.h"
#include "geo.h"
#include "heading.h"
#include "buttons.h"
#include "board.h"
#include "mag.h"
#include "ui.h"
#include "game.h"
#include "places_db.h"
#include "decl_grid.h"

enum class Mode { Run, Calibrate, Nap };
enum class Page : uint8_t { Taco, Compass, Level, Nacho, About, COUNT };

static Mode mode = Mode::Run;
static Page page = Page::Taco;
static float levelRoll0 = 0, levelPitch0 = 0;   // zero offsets for the level
static Vec3 levelAcc = {0, 0, 1};               // smoothed accel for a steady bubble
static Magnetometer mag;
static MagCal cal;
static MagCalibrator calibrator;
static Preferences prefs;
static Button bootBtn(LONG_PRESS_MS);
static NachoGame game;
static uint32_t lastTaps = 0;
static AngleSmoother smooth(HEADING_SMOOTH_ALPHA);

static Nearby nearest[NUM_NEAREST];
static size_t nearestCount = 0;
static int rank = 0;
static int32_t targetIdx = -1;
static bool homeSel = false;              // Home slot selected (it comes after the stores)
static bool homeSet = false;
static double homeLat = 0, homeLon = 0;
static bool comboActive = false, comboFired = false;
static uint32_t comboStart = 0, swallowPwrUntil = 0;
static uint32_t lastSwL = 0, lastSwR = 0, lastSwU = 0, lastSwD = 0;

static double posLat = 0, posLon = 0, lastSearchLat = 999, lastSearchLon = 999;
static bool havePos = false, posLive = false;
static double savedLat = 0, savedLon = 0;
static bool haveSaved = false;
static uint32_t lastSave = 0;
static bool simFix = false;
static double simLat = 0, simLon = 0;

static float headingTrue = 0;
static bool headingOk = false;
static char headingSrc = '-';
static bool useGpsCourse = false;

static uint32_t lastPwrCount = 0, lastFrame = 0, lastMag = 0, lastMotion = 0, calStart = 0;
static uint32_t napStart = 0, lastRaw = 0, lastSearch = 0;
static bool rawStream = false, ignoreBootUntilUp = false;
static Vec3 lastAccel = {0, 0, 1};
static Vec3 lastMagBoard = {0, 0, 0};

// ------------------------------------------------------------------ helpers
// Nacho Dog steering: rotation about the board's left-right axis, degrees.
// Raising the 12 o'clock edge (or tipping the top toward you when held upright) is "up".
static float gameTilt(const Vec3& a) { return atan2f(a.y, a.z) * RAD_TO_DEG; }

static char aboutStatus[48] = "";
static bool aboutStatusWarn = false;
static void drawAbout(uint32_t now) {
  AboutPage a;
  a.title = ABOUT_TITLE;
  a.line1 = ABOUT_LINE1;
  a.line2 = ABOUT_LINE2;
  a.line3 = ABOUT_LINE3;
  a.scroll = ABOUT_SCROLL;
  a.status = aboutStatus;
  a.statusWarn = aboutStatusWarn;
  ui::aboutPage(a, now);
}

static Vec3 magToBoard(const Vec3& s) {
  auto pick = [&](int axis, int sign) {
    float v = axis == 0 ? s.x : axis == 1 ? s.y : s.z;
    return sign < 0 ? -v : v;
  };
  Vec3 b = {pick(MAG_MAP_X_AXIS, MAG_MAP_X_SIGN), pick(MAG_MAP_Y_AXIS, MAG_MAP_Y_SIGN),
            pick(MAG_MAP_Z_AXIS, MAG_MAP_Z_SIGN)};
#if DISPLAY_FLIP_180
  b.x = -b.x;  // board turned 180 deg about the screen axis
  b.y = -b.y;
#endif
  return b;
}

// Labels are "name|street|city, ST"; any field may be empty.
static void placeText(uint32_t idx, PlaceText& t) {
  split_label(DB_LABELS + DB_LOCS[idx].label_ofs, t.name, t.street, t.city, sizeof(t.name));
  if (!t.street[0] && !t.city[0]) snprintf(t.street, sizeof(t.street), "no address listed");
}

// ---- look & feel (saved to flash; defaults come from the database / config.h)
static bool unitsMetric = DEFAULT_METRIC;
static Theme theme = (Theme)DB_THEME;
static const IconBitmap kIconL = {DB_ICON_L_W, DB_ICON_L_H, DB_ICON_L_PIX, DB_ICON_L_MASK};
static const IconBitmap kIconS = {DB_ICON_S_W, DB_ICON_S_H, DB_ICON_S_PIX, DB_ICON_S_MASK};

static void applyStyle() {
  if (theme == Theme::Icon && !DB_HAS_ICON) theme = Theme::Pin;
  ui::setStyle(theme, unitsMetric, DB_TITLE, DB_DEVICE_NAME, DB_HAS_ICON ? &kIconL : nullptr,
               DB_HAS_ICON ? &kIconS : nullptr);
}

static const char* themeName(Theme t) {
  return t == Theme::Taco ? "taco" : t == Theme::Pin ? "pin" : "icon";
}

static void loadPrefs() {
  prefs.begin("taco", false);
  if (prefs.getBytesLength("cal") == sizeof(MagCal)) prefs.getBytes("cal", &cal, sizeof(MagCal));
  haveSaved = prefs.getBool("fix", false);
  savedLat = prefs.getDouble("lat", 0);
  savedLon = prefs.getDouble("lon", 0);
  homeSet = prefs.getBool("hset", false);
  homeLat = prefs.getDouble("hlat", 0);
  homeLon = prefs.getDouble("hlon", 0);
  levelRoll0 = prefs.getFloat("lr0", 0);
  levelPitch0 = prefs.getFloat("lp0", 0);
  unitsMetric = prefs.getUChar("units", DEFAULT_METRIC) != 0;
  game.begin(prefs.getInt("nbest", 0));
  uint8_t th = prefs.getUChar("theme", 0xFF);
  if (th <= 2) theme = (Theme)th;
}

static void zeroLevel() {
  float r, p, t;
  if (level_angles(levelAcc, r, p, t) && t < 15) {
    levelRoll0 = r; levelPitch0 = p;
    prefs.putFloat("lr0", r);
    prefs.putFloat("lp0", p);
    ui::message("Level zeroed", "this surface now reads 0", "");
  } else {
    ui::message("Can't zero", "lay it flat, screen up,", "then hold BOOT again");
  }
  delay(1500);
}

static void saveFix(double lat, double lon) {
  prefs.putDouble("lat", lat);
  prefs.putDouble("lon", lon);
  prefs.putBool("fix", true);
  savedLat = lat; savedLon = lon; haveSaved = true;
}

static void searchNearest(bool force) {
  if (!havePos || DB_COUNT == 0) { nearestCount = 0; return; }
  uint32_t now = millis();
  float moved = geo_distance_m(posLat, posLon, lastSearchLat, lastSearchLon);
  if (!force && moved < 3.0f && now - lastSearch < 2000) return;
  lastSearch = now;
  lastSearchLat = posLat; lastSearchLon = posLon;
  nearestCount = geo_nearest(DB_LOCS, DB_COUNT, posLat, posLon, nearest, NUM_NEAREST);
  // Keep pointing at the same store even if the ranking shuffles as you move.
  rank = 0;
  for (size_t i = 0; i < nearestCount; i++)
    if ((int32_t)nearest[i].index == targetIdx) { rank = (int)i; break; }
  if (nearestCount) targetIdx = nearest[rank].index;
}

// Slots are: stores 0..nearestCount-1, then Home.
static void selectNext() {
  if (homeSel) { if (nearestCount) { homeSel = false; rank = 0; } }
  else if (rank + 1 < (int)nearestCount) rank++;
  else homeSel = true;
  if (!homeSel && nearestCount) targetIdx = nearest[rank].index;
}

static void selectPrev() {
  if (homeSel) { if (nearestCount) { homeSel = false; rank = (int)nearestCount - 1; } }
  else if (rank > 0) rank--;
  else homeSel = true;
  if (!homeSel && nearestCount) targetIdx = nearest[rank].index;
}

static void saveHome(double lat, double lon) {
  homeLat = lat; homeLon = lon; homeSet = true;
  prefs.putDouble("hlat", homeLat);
  prefs.putDouble("hlon", homeLon);
  prefs.putBool("hset", true);
}

static void clearHome() {
  homeSet = false;
  prefs.remove("hlat");
  prefs.remove("hlon");
  prefs.remove("hset");
}

static void setHomeHere() {
  if (!posLive || simFix) {
    ui::message("No GPS fix yet", "step outside and", "try again");
  } else {
    saveHome(posLat, posLon);
    homeSel = true;
    page = Page::Taco;
    ui::message("Home saved", "swipe to the house", "to point home");
  }
  delay(1800);
}

// PWR + BOOT held together. Returns true while the combo owns the buttons.
static bool handleCombo(bool pwrDown, bool bootDown, uint32_t now) {
  if (pwrDown && bootDown) {
    if (!comboActive) { comboActive = true; comboFired = false; comboStart = now; bootBtn.cancel(); }
    if (!comboFired && now - comboStart >= HOME_SET_HOLD_MS) {
      comboFired = true;
      setHomeHere();
    }
  }
  if (comboActive && !pwrDown && !bootDown) {
    comboActive = false;
    swallowPwrUntil = millis() + 1000;  // the AXP reports the PWR release as a press; eat it
  }
  return comboActive;
}

// ------------------------------------------------------------------ modes
static void enterNap(uint32_t now) {
  mode = Mode::Nap;
  napStart = now;
  ui::sleep();
  board::setNapping(true);
  Serial.println("nap");
}

static void leaveNap(uint32_t now) {
  board::setNapping(false);
  ui::wake(BRIGHTNESS);
  smooth.reset();
  mode = Mode::Run;
  lastMotion = now;
  ignoreBootUntilUp = digitalRead(PIN_BOOT_BTN) == LOW;
  Serial.println("wake");
}

static void startCalibration(uint32_t now) {
  if (mag.chip() == MagChip::None) {
    ui::message("No magnetometer", "check wiring to", "header pins 2-5");
    delay(2000);
    return;
  }
  calibrator.reset();
  calStart = now;
  mode = Mode::Calibrate;
}

static void runCalibration(uint32_t now, BtnEvent ev) {
  Vec3 raw;
  if (mag.read(raw)) calibrator.add(magToBoard(raw));
  uint32_t el = now - calStart;
  if (ev == BtnEvent::Short) {  // cancel
    mode = Mode::Run;
    return;
  }
  if (el >= CAL_SECONDS * 1000UL) {
    MagCal c;
    if (calibrator.finish(c)) {
      cal = c;
      prefs.putBytes("cal", &cal, sizeof(MagCal));
      smooth.reset();
      ui::message("Calibrated", "nice tumbling", "");
    } else {
      ui::message("Not enough", "turn it every which way", "hold BOOT to retry");
    }
    Serial.printf("cal off=%.1f,%.1f,%.1f scale=%.3f,%.3f,%.3f minRange=%.1f\n", c.offset[0],
                  c.offset[1], c.offset[2], c.scale[0], c.scale[1], c.scale[2],
                  calibrator.minRange());
    delay(1800);
    mode = Mode::Run;
    return;
  }
  if (now - lastFrame >= 66) {
    lastFrame = now;
    ui::calibrating(el / (CAL_SECONDS * 1000.0f), calibrator.coverage(),
                    (int)((CAL_SECONDS * 1000UL - el + 999) / 1000));
  }
}

static void napLoop() {
  // Light sleep in short slices; GPS stays powered so the fix is warm on wake.
  gpio_wakeup_enable((gpio_num_t)PIN_BOOT_BTN, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(200 * 1000);
  esp_light_sleep_start();
  uint32_t now = millis();
  board::pollPowerKey();
  BoardSnapshot s;
  board::snapshot(s);
  if (s.pwrShortPresses != lastPwrCount || digitalRead(PIN_BOOT_BTN) == LOW) {
    lastPwrCount = s.pwrShortPresses;
    leaveNap(now);
    return;
  }
  if (POWEROFF_AFTER_NAP_S && now - napStart > POWEROFF_AFTER_NAP_S * 1000UL) {
    ui::wake(BRIGHTNESS);
    ui::message("Bye", "press PWR to turn on", "");
    delay(1500);
    board::powerOff();
  }
}

// ------------------------------------------------------------------ serial
// Magnetometer bus diagnostics: idle line levels, then an address scan, for both pin orders.
static void magScan() {
  const int pins[2][2] = {{PIN_MAG_SDA, PIN_MAG_SCL}, {PIN_MAG_SCL, PIN_MAG_SDA}};
  for (auto& p : pins) {
    Wire1.end();
    pinMode(p[0], INPUT);
    pinMode(p[1], INPUT);
    delay(5);
    int sdaIdle = digitalRead(p[0]), sclIdle = digitalRead(p[1]);
    pinMode(p[0], INPUT_PULLDOWN);
    pinMode(p[1], INPUT_PULLDOWN);
    delay(5);
    int sdaPd = digitalRead(p[0]), sclPd = digitalRead(p[1]);
    Serial.printf("SDA=GPIO%d SCL=GPIO%d  idle: SDA=%d SCL=%d  (with weak pull-down: %d %d)\n", p[0], p[1],
                  sdaIdle, sclIdle, sdaPd, sclPd);
    Serial.println(sdaPd && sclPd ? "  lines are pulled up: breakout powered and connected"
                                  : "  a line is NOT pulled up: check power/GND and that wire");
    Wire1.begin(p[0], p[1], 100000);
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
      Wire1.beginTransmission(a);
      if (Wire1.endTransmission() == 0) { Serial.printf("  found 0x%02X\n", a); found++; }
    }
    Serial.printf("  %d device(s)%s\n", found, found ? "" : " (LIS3MDL should be 0x1C or 0x1E)");
  }
  Wire1.end();
  Wire1.begin(PIN_MAG_SDA, PIN_MAG_SCL, MAG_I2C_HZ);
  bool ok = mag.begin(Wire1);
  if (!ok) { Wire1.end(); Wire1.begin(PIN_MAG_SCL, PIN_MAG_SDA, MAG_I2C_HZ); ok = mag.begin(Wire1); }
  Serial.printf("magnetometer: %s\n", mag.name());
}

static void printHelp() {
  Serial.println(
      "TacoCompass serial commands (115200 baud, end with Enter):\n"
      "  ?  | help              this list\n"
      "  info                   status, GPS, heading, 5 nearest\n"
      "  home                   show Home\n"
      "  home <lat>,<lon>       set Home to coordinates (saved to flash)\n"
      "  home here              set Home to the current live GPS fix\n"
      "  home clear             forget Home\n"
      "  units                  show units\n"
      "  units freedom|metric   ft/mi/mph or m/km/km/h (saved)\n"
      "  theme                  show theme\n"
      "  theme taco|pin|icon    place marker + colors (saved); 'theme default' = database's choice\n"
      "  raw                    toggle streaming accel/mag/heading (axis mapping)\n"
      "  scan                   list devices on the onboard I2C bus\n"
      "  magscan                check the magnetometer wiring (line levels + address scan)\n"
      "  nap                    nap now (PWR or BOOT wakes it; USB serial drops while asleep)\n"
      "  gpstest                run one GPS poll with its steps printed\n"
      "  gpsuart                listen for GPS NMEA on GPIO18/17 (serial routing)\n"
      "  gpsreset               reset the GPS module (pulse its RESET line)\n"
      "  gpstrim                tell the GPS to send only GGA+RMC (stays until long power-off)\n"
      "  cal                    start magnetometer calibration\n"
      "  uncal                  forget magnetometer calibration\n"
      "  unlevel                forget the bubble-level zero\n"
      "  fix <lat>,<lon>        fake a GPS position (desk testing; not saved)\n"
      "  fix off                stop faking");
}

static void printHome() {
  if (homeSet)
    Serial.printf("home %.6f,%.6f  (%.0f m from %s position)\n", homeLat, homeLon,
                  havePos ? geo_distance_m(posLat, posLon, homeLat, homeLon) : 0.0f,
                  havePos ? (posLive ? "current" : "last known") : "no");
  else
    Serial.println("home not set (hold PWR + BOOT on the device, or: home <lat>,<lon>)");
}

static void homeCommand(const char* arg) {
  double la, lo;
  while (*arg == ' ') arg++;
  if (!*arg) { printHome(); return; }
  if (!strcmp(arg, "clear")) { clearHome(); Serial.println("home cleared"); return; }
  if (!strcmp(arg, "here")) {
    if (!posLive || simFix) { Serial.println("no live GPS fix; can't use 'here'"); return; }
    saveHome(posLat, posLon);
    printHome();
    return;
  }
  if (geo_parse_latlon(arg, la, lo)) { saveHome(la, lo); printHome(); return; }
  Serial.println("usage: home | home <lat>,<lon> | home here | home clear   e.g. home 34.2073,-84.1402");
}

static void unitsCommand(const char* arg) {
  while (*arg == ' ') arg++;
  if (!strcmp(arg, "freedom")) unitsMetric = false;
  else if (!strcmp(arg, "metric")) unitsMetric = true;
  else if (*arg) { Serial.println("usage: units | units freedom | units metric"); return; }
  if (*arg) { prefs.putUChar("units", unitsMetric); applyStyle(); }
  Serial.printf("units: %s\n", unitsMetric ? "metric" : "Freedom Units (ft/mi/mph)");
}

static void themeCommand(const char* arg) {
  while (*arg == ' ') arg++;
  if (!strcmp(arg, "taco")) theme = Theme::Taco;
  else if (!strcmp(arg, "pin")) theme = Theme::Pin;
  else if (!strcmp(arg, "icon")) {
    if (!DB_HAS_ICON) { Serial.println("this database has no icon (build_db.py --icon file.png)"); return; }
    theme = Theme::Icon;
  } else if (!strcmp(arg, "default")) { theme = (Theme)DB_THEME; prefs.remove("theme"); applyStyle(); }
  else if (*arg) { Serial.println("usage: theme | theme taco|pin|icon|default"); return; }
  if (*arg && strcmp(arg, "default")) { prefs.putUChar("theme", (uint8_t)theme); applyStyle(); }
  Serial.printf("theme: %s%s\n", themeName(theme), DB_HAS_ICON ? " (icon available)" : "");
}

static void serialCommands() {
  static char line[64];
  static int n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (n < (int)sizeof(line) - 1) line[n++] = c;
      continue;
    }
    while (n > 0 && line[n - 1] == ' ') n--;  // trailing spaces
    line[n] = 0;
    n = 0;
    if (!line[0]) continue;
    double la, lo;
    if (!strcmp(line, "?") || !strcmp(line, "help")) printHelp();
    else if (!strcmp(line, "raw")) rawStream = !rawStream;
    else if (!strcmp(line, "scan")) board::i2cScan(Serial);
    else if (!strcmp(line, "magscan")) magScan();
    else if (!strcmp(line, "nap")) { Serial.flush(); enterNap(millis()); }
    else if (!strcmp(line, "gpstest")) board::gpsProbe(Serial);
    else if (!strcmp(line, "gpsuart")) board::gpsUartListen(Serial);
    else if (!strcmp(line, "gpsreset")) board::gpsReset();
    else if (!strcmp(line, "gpstrim")) board::gpsTrim();
    else if (!strcmp(line, "cal")) startCalibration(millis());
    else if (!strcmp(line, "uncal")) { cal = MagCal(); prefs.remove("cal"); Serial.println("cal cleared"); }
    else if (!strcmp(line, "unlevel")) {
      levelRoll0 = levelPitch0 = 0;
      prefs.remove("lr0");
      prefs.remove("lp0");
      Serial.println("level zero cleared");
    } else if (!strcmp(line, "home") || !strncmp(line, "home ", 5)) homeCommand(line + 4);
    else if (!strcmp(line, "units") || !strncmp(line, "units ", 6)) unitsCommand(line + 5);
    else if (!strcmp(line, "theme") || !strncmp(line, "theme ", 6)) themeCommand(line + 5);
    else if (!strcmp(line, "fix off")) { simFix = false; Serial.println("sim off"); }
    else if (!strncmp(line, "fix ", 4) && geo_parse_latlon(line + 4, la, lo)) {
      simLat = la; simLon = lo;
      simFix = true;
      Serial.printf("sim fix %.6f,%.6f\n", simLat, simLon);
    } else if (!strcmp(line, "info")) {
      BoardSnapshot s;
      board::snapshot(s);
      Serial.printf("%s: %d x %s (built %s) theme=%s units=%s\n", DB_DEVICE_NAME, DB_COUNT, DB_TITLE,
                    DB_BUILD_DATE, themeName(theme), unitsMetric ? "metric" : "freedom");
      Serial.printf("mag=%s cal=%d imu=%d pmu=%d touch=%d\n", mag.name(), cal.valid, board::imuOk(),
                    board::pmuOk(), board::touchOk());
      Serial.printf("gps valid=%d sats=%d hdop=%.1f %.6f,%.6f spd=%.1f gpsBytes=%u\n", s.fix.valid,
                    s.fix.sats, s.fix.hdop, s.fix.lat, s.fix.lon, s.fix.speed_mps, s.gpsBytes);
      if (GPS_USE_UART)
        Serial.printf("gps link: UART RX=GPIO%d @%d, %u bytes received\n", GPS_UART_RX, GPS_UART_BAUD,
                      s.gpsBytes);
      else
        Serial.printf("gps link: I2C polls=%u errs=%u unsticks=%u lastLen=%u\n", s.gpsPolls,
                      s.gpsI2cErrs, s.gpsUnsticks, s.gpsLastLen);
      if (s.gpsReadyMs)
        Serial.printf("gps resets=%u answered %u ms after last reset, cfgAcks=%u (4 per trim)\n",
                      s.gpsResets, s.gpsReadyMs - 1, s.gpsCfgAcks);
      else
        Serial.printf("gps resets=%u, did NOT answer after last reset, cfgAcks=%u\n", s.gpsResets,
                      s.gpsCfgAcks);
      Serial.printf("heading=%.1f src=%c batt=%d%% level0=%.1f,%.1f\n", headingTrue, headingSrc,
                    s.battPct, levelRoll0, levelPitch0);
      printHome();
      for (size_t i = 0; i < nearestCount; i++) {
        PlaceText t;
        placeText(nearest[i].index, t);
        Serial.printf(" %u. %.0f m @%.0f  %s%s%s, %s\n", (unsigned)i + 1, nearest[i].meters,
                      nearest[i].bearing_deg, t.name, t.name[0] ? " - " : "", t.street, t.city);
      }
    } else Serial.printf("unknown command '%s' (type ? for help)\n", line);
  }
}

// ------------------------------------------------------------------ setup/loop
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  loadPrefs();

  bool boardOk = board::begin();
  applyStyle();
  if (!ui::begin(BRIGHTNESS)) {
    Serial.println("display init failed (is PSRAM enabled? Tools > PSRAM > OPI PSRAM)");
  }
  Wire1.begin(PIN_MAG_SDA, PIN_MAG_SCL, MAG_I2C_HZ);
  bool magOk = mag.begin(Wire1);
  if (!magOk) {  // SDA/SCL wired the other way round? Try swapped before giving up.
    Wire1.end();
    Wire1.begin(PIN_MAG_SCL, PIN_MAG_SDA, MAG_I2C_HZ);
    magOk = mag.begin(Wire1);
    if (magOk)
      Serial.printf("magnetometer found with SDA/SCL swapped (SDA=GPIO%d, SCL=GPIO%d): fix config.h\n",
                    PIN_MAG_SCL, PIN_MAG_SDA);
  }
  Serial.printf("TacoCompass: board=%d mag=%s db=%d\n", boardOk, mag.name(), DB_COUNT);

  aboutStatusWarn = !magOk;
  if (magOk) {  // "8,861 Taco Bells"
    char num[16];
    int n = DB_COUNT;
    if (n >= 1000) snprintf(num, sizeof(num), "%d,%03d", n / 1000, n % 1000);
    else snprintf(num, sizeof(num), "%d", n);
    size_t tl = strlen(DB_TITLE);
    bool plural = n != 1 && tl && DB_TITLE[tl - 1] != 's';
    snprintf(aboutStatus, sizeof(aboutStatus), "%s %s%s", num, DB_TITLE, plural ? "s" : "");
  }
  else snprintf(aboutStatus, sizeof(aboutStatus), "no magnetometer!");
  for (uint32_t t0 = millis(); millis() - t0 < (uint32_t)ABOUT_BOOT_MS;) {
    drawAbout(millis());
    delay(30);
  }
  lastMotion = millis();
  if (magOk && !cal.valid) startCalibration(millis());
}

void loop() {
  if (mode == Mode::Nap) { napLoop(); return; }

  uint32_t now = millis();
  serialCommands();
  if (mode == Mode::Nap) return;  // the 'nap' command
  BoardSnapshot s;
  board::snapshot(s);
  ui::setBattery(s.battPct, s.charging);

  // ---- buttons
  bool bootDown = digitalRead(PIN_BOOT_BTN) == LOW;
  if (ignoreBootUntilUp && !bootDown) ignoreBootUntilUp = false;
  bool combo = mode == Mode::Run && handleCombo(s.pwrDown, bootDown, now);
  BtnEvent ev = bootBtn.update(bootDown && !ignoreBootUntilUp, now);
  if (combo) { ev = BtnEvent::None; bootBtn.cancel(); }  // BOOT release after the combo is not a "next"
  if (ev != BtnEvent::None || combo || s.pwrDown) lastMotion = now;
  if (s.pwrShortPresses != lastPwrCount) {
    lastPwrCount = s.pwrShortPresses;
    bool swallowed = combo || (int32_t)(now - swallowPwrUntil) < 0;
    if (mode == Mode::Run && !swallowed) { enterNap(now); return; }
  }
  int swipeNext = (int)(s.swipesLeft - lastSwL), swipePrev = (int)(s.swipesRight - lastSwR);
  int pageNext = (int)(s.swipesUp - lastSwU), pagePrev = (int)(s.swipesDown - lastSwD);
  lastSwL = s.swipesLeft; lastSwR = s.swipesRight; lastSwU = s.swipesUp; lastSwD = s.swipesDown;
  if (now - s.lastTouchMs < 1000) lastMotion = now;

  if (mode == Mode::Calibrate) { runCalibration(now, ev); return; }

  // ---- Nacho Dog: taps start a game; while playing, swipes and BOOT belong to the game
  uint32_t taps = s.taps - lastTaps;
  lastTaps = s.taps;
  if (page == Page::Nacho) {
    if (game.playing()) {
      lastMotion = now;  // tilting is the whole point: never auto-nap mid-game
      if (ev != BtnEvent::None) { game.end(now); ev = BtnEvent::None; }
      pageNext = pagePrev = 0;
    } else if (taps && s.accelOk) {
      game.start(now, gameTilt(s.accel), esp_random());
    }
  }

  const int nPages = (int)Page::COUNT;
  int pageStep = pageNext - pagePrev + (ev == BtnEvent::Short ? 1 : 0);
  if (pageStep) {
    page = (Page)((((int)page + pageStep) % nPages + nPages) % nPages);
    ui::pageHint((int)page, nPages, now + 1200);
  }
  if (page == Page::Taco) {
    for (int i = 0; i < swipeNext; i++) selectNext();
    for (int i = 0; i < swipePrev; i++) selectPrev();
  }
  if (ev == BtnEvent::Long) {
    if (page == Page::Level) zeroLevel();
    else startCalibration(now);
    return;
  }

  // ---- position
  if (simFix) { posLat = simLat; posLon = simLon; havePos = true; posLive = true; }
  else if (s.fix.valid) { posLat = s.fix.lat; posLon = s.fix.lon; havePos = true; posLive = true; }
  else if (haveSaved) { posLat = savedLat; posLon = savedLon; havePos = true; posLive = false; }
  else { havePos = false; posLive = false; }

  if (posLive && !simFix && now - lastSave > SAVE_FIX_EVERY_S * 1000UL &&
      (!haveSaved || geo_distance_m(posLat, posLon, savedLat, savedLon) > 100.0f)) {
    lastSave = now;
    saveFix(posLat, posLon);
  }
  searchNearest(false);
  if (!nearestCount) homeSel = true;  // no stores in reach (or empty DB): Home is the only slot

  // ---- motion (for auto-nap)
  Vec3 a = s.accel;
  float da = fabsf(a.x - lastAccel.x) + fabsf(a.y - lastAccel.y) + fabsf(a.z - lastAccel.z);
  if (da > 0.06f) lastMotion = now;
  lastAccel = a;

  // ---- heading
  float decl = havePos ? geo_declination_deg(DECL_GRID, DECL_LAT0, DECL_LON0, DECL_STEP,
                                             DECL_ROWS, DECL_COLS, posLat, posLon)
                       : 0.0f;
  float spd = s.fix.valid ? s.fix.speed_mps : 0.0f;
  if (useGpsCourse && spd < GPS_HEADING_MIN_MPS - GPS_HEADING_HYST_MPS) useGpsCourse = false;
  if (!useGpsCourse && spd > GPS_HEADING_MIN_MPS && s.fix.course_valid) useGpsCourse = true;

  if (now - lastMag >= 20) {
    lastMag = now;
    Vec3 raw;
    bool magRead = mag.read(raw);
    if (magRead) lastMagBoard = magToBoard(raw);
    float h;
    if (useGpsCourse && s.fix.course_valid && !simFix) {
      smooth.setAlpha(0.35f);
      headingTrue = smooth.update(s.fix.course_deg);
      headingOk = true;
      headingSrc = 'G';
    } else if (magRead && cal.valid && s.accelOk && tilt_heading(a, cal.apply(lastMagBoard), h)) {
      smooth.setAlpha(HEADING_SMOOTH_ALPHA);
      headingTrue = smooth.update(wrap360(h + decl));
      headingOk = true;
      headingSrc = 'M';
    } else if (!magRead || !cal.valid) {
      headingOk = false;
      headingSrc = '-';
    }
  }

  if (rawStream && now - lastRaw >= 200) {
    lastRaw = now;
    Serial.printf("acc %.2f %.2f %.2f | mag %.1f %.1f %.1f | hdg %.1f%c decl %.1f\n", a.x, a.y,
                  a.z, lastMagBoard.x, lastMagBoard.y, lastMagBoard.z, headingTrue, headingSrc,
                  decl);
  }

  // ---- smoothed accel for the level page (time-constant ~150 ms)
  if (s.accelOk) {
    static uint32_t lastLev = 0;
    float k = fminf(1.0f, (now - lastLev) / 150.0f);
    lastLev = now;
    levelAcc = {levelAcc.x + k * (a.x - levelAcc.x), levelAcc.y + k * (a.y - levelAcc.y),
                levelAcc.z + k * (a.z - levelAcc.z)};
  }

  // ---- auto-nap (a level sits still on purpose, so give it longer)
  uint32_t stillLimitS = page == Page::Level ? 600 : NAP_AFTER_STILL_S;
  if (NAP_AFTER_STILL_S && !s.usb && now - lastMotion > stillLimitS * 1000UL) {
    enterNap(now);
    return;
  }

  // ---- Nacho Dog runs at ~30 fps
  if (page == Page::Nacho) {
    if (s.accelOk) game.update(now, gameTilt(s.accel));
    if (game.bestChanged()) { prefs.putInt("nbest", game.best()); game.clearBestChanged(); }
    if (now - lastFrame < 33) return;
    lastFrame = now;
    ui::nachoPage(game.view(now));
    return;
  }

  // ---- draw (~15 fps)
  if (now - lastFrame < 66) return;
  lastFrame = now;

  if (page == Page::About) {
    drawAbout(now);
    return;
  }

  if (page == Page::Level) {
    LevelPage l;
    float r, p, t;
    l.ok = s.accelOk && level_angles(levelAcc, r, p, t);
    if (l.ok) {
      l.rollDeg = r - levelRoll0;
      l.pitchDeg = p - levelPitch0;
      l.tiltDeg = t;  // raw tilt decides "lay it flat"
    }
    l.zeroed = levelRoll0 != 0 || levelPitch0 != 0;
    ui::levelPage(l);
    return;
  }

  if (page == Page::Compass) {
    CompassPage c;
    c.headingOk = headingOk;
    c.headingDeg = headingTrue;
    c.src = headingSrc;
    c.havePos = havePos;
    c.liveFix = posLive;
    c.lat = posLat;
    c.lon = posLon;
    c.declDeg = decl;
    c.altOk = posLive && s.fix.alt_valid && !simFix;
    c.altM = s.fix.alt_m;
    c.speedMps = posLive ? s.fix.speed_mps : 0;
    float r, p, t;
    c.tiltDeg = level_angles(a, r, p, t) ? t : 0;
    c.magMissing = mag.chip() == MagChip::None;
    c.magUncal = !c.magMissing && !cal.valid;
    ui::compassPage(c);
    return;
  }

  if (!havePos) { ui::searching(s.fix.sats, now, DB_COUNT > 0); return; }

  CompassView v;
  v.haveTarget = nearestCount > 0;
  v.headingOk = headingOk;
  v.headingDeg = headingOk ? headingTrue : 0;
  v.headingSrc = headingSrc;
  v.sats = s.fix.sats;
  v.liveFix = posLive;
  v.battPct = s.battPct;
  v.charging = s.charging;
  v.magMissing = mag.chip() == MagChip::None;
  v.magUncal = !v.magMissing && !cal.valid;
  static PlaceText txt;
  v.count = (int)nearestCount;
  v.rank = rank;
  if (homeSel) {
    v.isHome = homeSet;
    v.homeUnset = !homeSet;
    v.haveTarget = homeSet;
    if (homeSet) {
      v.meters = geo_distance_m(posLat, posLon, homeLat, homeLon);
      v.arrowDeg = wrap360(geo_bearing_deg(posLat, posLon, homeLat, homeLon) - v.headingDeg);
      v.arrived = posLive && v.meters < ARRIVED_METERS;
    }
  } else if (v.haveTarget) {
    const Nearby& t = nearest[rank];
    v.meters = t.meters;
    v.arrowDeg = wrap360(t.bearing_deg - v.headingDeg);
    v.arrived = posLive && t.meters < ARRIVED_METERS;
    placeText(t.index, txt);
    v.name = txt.name;
    v.street = txt.street;
    v.cityState = txt.city;
  }
  ui::compass(v, now);
}
