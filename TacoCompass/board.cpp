#include "board.h"
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101  // selects XPowersPMU = XPowersAXP2101
#include <XPowersLib.h>
#include <SensorQMI8658.hpp>
#include <TouchDrvCSTXXX.hpp>
#include "buttons.h"
#include "config.h"

namespace {

XPowersPMU pmu;
SensorQMI8658 imu;
TouchDrvCST92xx touch;
bool pmuOk_ = false, imuOk_ = false, touchOk_ = false;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
BoardSnapshot shared;        // guarded by mux
volatile bool napping = false;
volatile bool napIdle = false;  // sensor task has parked (no I2C transfer in flight)
volatile bool touchIrq = false;
NmeaParser nmea;
GpsFix fixWork;
SwipeDetector swipe(SWIPE_MIN_PX);

// ---------------- LC76G over I2C ----------------
// Rules from Quectel's LC76G I2C application note and Quectel staff guidance:
//  * no other I2C device may be accessed DURING an LC76G transaction, or the module
//    can jam (0x50 ACKs, 0x54 NACKs) until a long power-off;
//  * settle before each step (the note says >= 10 ms; working drivers use 100 ms, and at
//    12 ms the length query answered but always said 0), each step its own I2C frame;
//  * reads on 0x54 can clock-stretch ~200 ms; read <= 512 bytes at a time and drain to 0;
//  * a transaction cut off mid-way (e.g. by an ESP32 reset or upload) leaves the module
//    ACKing 0x50 but NACKing 0x54; reading 1 byte from 0x54 and from 0x58 ends it
//    (Quectel forum, "LC76G No NMEA messages over I2C");
//  * the module runs off the always-on 3V3 rail, so an ESP32 reset never power-cycles it.
//    Pulsing RESET (TCA9554 P7) low for 100 ms at every boot, and again after 30 s with
//    no NMEA, recovers a wedged module (capsule-radar PR #29, same board);
//  * keep config writes rare (~88 cumulative writes reportedly corrupt its parser).
// So a whole poll runs under busLock, and every I2C0 user outside this task takes it too.
SemaphoreHandle_t busLock = nullptr;
const uint8_t GPS_CMD = 0x50, GPS_RD = 0x54, GPS_WR = 0x58;
const size_t GPS_CHUNK = 512;
const uint32_t GPS_GAP_MS = 100, GPS_PERIOD_MS = 1000;  // 100 ms settle, as Waveshare + capsule-radar
const int GPS_MAX_CHUNKS = 8;          // per poll, ~4 KB = the module's whole TX buffer
const uint32_t GPS_BUSY = 0xFFFFFFFF;  // module says "not now"
const uint32_t GPS_STALL_MS = 30000;      // no NMEA this long -> reset the module
const uint32_t GPS_RESET_GAP_MS = 60000;  // at most one stall reset per minute
const uint32_t GPS_READY_WAIT_MS = 1500;  // after reset, wait this long for it to answer
const int GPS_CFG_MAX_WRITES = 10;        // config writes per boot, ever
uint8_t gpsBuf[GPS_CHUNK];
uint32_t gpsErrStreak = 0;
uint32_t gpsLastDataMs = 0, gpsLastResetMs = 0;
int gpsCfgWrites = 0;
volatile bool gpsResetRequested = false, gpsTrimRequested = false;

struct BusGuard {
  BusGuard() { xSemaphoreTake(busLock, portMAX_DELAY); }
  ~BusGuard() { xSemaphoreGive(busLock); }
};

void gpsGap() { vTaskDelay(pdMS_TO_TICKS(GPS_GAP_MS)); }

bool gpsCommand(uint32_t cmd, uint32_t len) {
  uint8_t b[8] = {(uint8_t)cmd, (uint8_t)(cmd >> 8), (uint8_t)(cmd >> 16), (uint8_t)(cmd >> 24),
                  (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24)};
  Wire.beginTransmission(GPS_CMD);
  Wire.write(b, sizeof(b));
  return Wire.endTransmission() == 0;
}

size_t gpsReceive(uint8_t* d, size_t n) {
  size_t got = Wire.requestFrom((uint16_t)GPS_RD, n, true);
  for (size_t i = 0; i < got; i++) d[i] = Wire.read();
  return got;
}

const char* gpsLastFail = "";  // which step failed last (for gpstest)

// "How many bytes are waiting?"  Returns false on a bus error.
bool gpsQueryLen(uint32_t& len) {
  gpsGap();
  if (!gpsCommand(0xAA510008, 4)) { gpsLastFail = "length query: write to 0x50 NACKed"; return false; }
  gpsGap();
  uint8_t l[4];
  if (gpsReceive(l, 4) != 4) { gpsLastFail = "length query: read from 0x54 NACKed"; return false; }
  len = l[0] | (l[1] << 8) | (l[2] << 16) | ((uint32_t)l[3] << 24);
  return true;
}

// Read n (<= GPS_CHUNK) bytes of NMEA into gpsBuf.
size_t gpsReadChunk(uint32_t n) {
  gpsGap();
  if (!gpsCommand(0xAA512000, n)) { gpsLastFail = "data request: write to 0x50 NACKed"; return 0; }
  gpsGap();
  size_t r = gpsReceive(gpsBuf, n);
  if (r != n) gpsLastFail = "data read: short read from 0x54";
  return r;
}

// End a half-finished transaction so the module answers at 0x50 again.
void gpsUnstick() {
  gpsGap();
  if (Wire.requestFrom((uint16_t)GPS_RD, (size_t)1, true)) Wire.read();
  gpsGap();
  if (Wire.requestFrom((uint16_t)GPS_WR, (size_t)1, true)) Wire.read();
}

void feedNmea(const uint8_t* d, size_t n);  // below

// One complete, uninterrupted poll: drain everything that's waiting.
void gpsPoll(Print* verbose) {
  BusGuard g;
  uint32_t got = 0;
  bool err = false;
  uint32_t lastLen = 0;
  for (int c = 0; c < GPS_MAX_CHUNKS; c++) {
    uint32_t len;
    if (!gpsQueryLen(len)) { err = true; break; }
    lastLen = len;
    if (verbose) verbose->printf("  waiting: %u bytes\n", (unsigned)len);
    if (len == 0 || len == GPS_BUSY) break;
    uint32_t n = len < GPS_CHUNK ? len : GPS_CHUNK;
    size_t r = gpsReadChunk(n);
    if (r != n) { err = true; break; }
    if (verbose) {
      for (size_t i = 0; i < r && i < 160; i++) {
        char ch = (char)gpsBuf[i];
        verbose->write((ch >= 32 && ch < 127) || ch == '\n' ? (uint8_t)ch : (uint8_t)'.');
      }
      verbose->println(r > 160 ? " ..." : "");
    }
    feedNmea(gpsBuf, r);
    got += r;
    if (len <= GPS_CHUNK) break;  // that was the last of it
  }
  if (err) {
    if (verbose) verbose->printf("  failed at: %s (clearing it)\n", gpsLastFail);
    gpsUnstick();
  }
  if (err) gpsErrStreak++; else gpsErrStreak = 0;
  if (got) gpsLastDataMs = millis();
  portENTER_CRITICAL(&mux);
  shared.gpsPolls++;
  if (err) { shared.gpsI2cErrs++; shared.gpsUnsticks++; }
  shared.gpsLastLen = lastLen;
  shared.gpsBytes += got;
  portEXIT_CRITICAL(&mux);
}

void feedNmea(const uint8_t* d, size_t n) {
  static const char kAck[] = "$PAIR001,062,0";  // config acknowledged
  static size_t ackPos = 0;
  uint32_t acks = 0;
  bool changed = false;
  for (size_t i = 0; i < n; i++) {
    char ch = (char)d[i];
    changed |= nmea.feed(ch, fixWork);
    if (ch == kAck[ackPos]) {
      if (!kAck[++ackPos]) { acks++; ackPos = 0; }
    } else {
      ackPos = ch == '$' ? 1 : 0;
    }
  }
  portENTER_CRITICAL(&mux);
  shared.gpsCfgAcks += acks;
  shared.fix.sats = fixWork.sats;
  shared.fix.hdop = fixWork.hdop;
  if (changed || !fixWork.valid) shared.fix = fixWork;
  portEXIT_CRITICAL(&mux);
}

// ---------------- helpers ----------------
inline float pick(float x, float y, float z, int axis, int sign) {
  float v = axis == 0 ? x : axis == 1 ? y : z;
  return sign < 0 ? -v : v;
}

void readAccel() {
  if (!imuOk_ || !imu.getDataReady()) return;
  float x, y, z;
  if (imu.getAccelerometer(x, y, z)) {
    Vec3 a = {pick(x, y, z, ACC_MAP_X_AXIS, ACC_MAP_X_SIGN),
              pick(x, y, z, ACC_MAP_Y_AXIS, ACC_MAP_Y_SIGN),
              pick(x, y, z, ACC_MAP_Z_AXIS, ACC_MAP_Z_SIGN)};
#if DISPLAY_FLIP_180
    a.x = -a.x;  // board turned 180 deg about the screen axis
    a.y = -a.y;
#endif
    portENTER_CRITICAL(&mux);
    shared.accel = a;
    shared.accelOk = true;
    portEXIT_CRITICAL(&mux);
  }
}

// TCA9554 input port, bit 4 = conditioned PWR level (high while pressed).
void readPwrLevel() {
  Wire.beginTransmission(0x20);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return;
  if (Wire.requestFrom((uint8_t)0x20, (size_t)1) != 1) return;
  bool down = (Wire.read() & 0x10) != 0;
  portENTER_CRITICAL(&mux);
  shared.pwrDown = down;
  portEXIT_CRITICAL(&mux);
}

int tcaRead(uint8_t reg) {
  Wire.beginTransmission(0x20);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((uint8_t)0x20, (size_t)1) != 1) return -1;
  return Wire.read();
}

bool tcaWrite(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(0x20);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

// ---------------- LC76G reset + recovery ----------------
// Caller holds busLock. Pulls RESET (TCA9554 P7) low for 100 ms, then releases it by
// making P7 an input again (its power-on state) so the module's own pull-up holds it high.
// If P7 doesn't read back high, drive it high instead so it can never float into reset.
bool gpsResetPulse() {
  int o = tcaRead(0x01), c = tcaRead(0x03);
  if (o < 0 || c < 0) { Serial.println("[gps] reset: TCA9554 not answering"); return false; }
  tcaWrite(0x01, (uint8_t)(o & 0x7F));  // latch low first...
  tcaWrite(0x03, (uint8_t)(c & 0x7F));  // ...then P7 output = RESET asserted
  vTaskDelay(pdMS_TO_TICKS(5));
  int in = tcaRead(0x00);
  bool lowOk = in >= 0 && !(in & 0x80);
  vTaskDelay(pdMS_TO_TICKS(100));
  tcaWrite(0x01, (uint8_t)(o | 0x80));  // latch high, in case P7 is ever an output again
  bool rel = false;
  for (int i = 0; i < 3 && !rel; i++) rel = tcaWrite(0x03, (uint8_t)(c | 0x80));  // input
  vTaskDelay(pdMS_TO_TICKS(5));
  in = tcaRead(0x00);
  bool highOk = in >= 0 && (in & 0x80);
  if (!highOk) tcaWrite(0x03, (uint8_t)(c & 0x7F));  // no pull-up after all: drive high
  Serial.printf("[gps] reset pulse: P7 low=%s released=%s\n", lowOk ? "ok" : "FAIL",
                highOk ? "ok" : "no pull-up, driving high");
  return lowOk;
}

// Caller holds busLock. Ask "how many bytes waiting?" until the module answers.
bool gpsAwaitReady(uint32_t& tookMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < GPS_READY_WAIT_MS) {
    vTaskDelay(pdMS_TO_TICKS(100));
    uint32_t len;
    if (gpsQueryLen(len)) {
      tookMs = millis() - t0;
      // Finish the exchange the way Waveshare's example does: if it reported data, read it.
      if (len != 0 && len != GPS_BUSY) {
        uint32_t n = len < GPS_CHUNK ? len : GPS_CHUNK;
        size_t r = gpsReadChunk(n);
        if (r == n) { feedNmea(gpsBuf, r); gpsLastDataMs = millis(); }
        else gpsUnstick();
        Serial.printf("[gps] first read after reset: %u of %u bytes\n", (unsigned)r, (unsigned)len);
      }
      return true;
    }
    gpsUnstick();
  }
  return false;
}

// Caller holds busLock. Send bytes to the module via its I2C write path:
// free-space query, length announcement on 0x50, then the bytes on 0x58.
// Quectel's working drivers settle 100 ms between steps here.
bool gpsWriteBytes(const uint8_t* d, size_t n) {
  vTaskDelay(pdMS_TO_TICKS(100));
  if (!gpsCommand(0xAA510004, 4)) return false;
  vTaskDelay(pdMS_TO_TICKS(100));
  uint8_t f[4];
  if (gpsReceive(f, 4) != 4) { gpsUnstick(); return false; }
  uint32_t freeSpace = f[0] | (f[1] << 8) | (f[2] << 16) | ((uint32_t)f[3] << 24);
  if (freeSpace < n || freeSpace == GPS_BUSY) return false;
  vTaskDelay(pdMS_TO_TICKS(100));
  if (!gpsCommand(0xAA531000, n)) return false;
  vTaskDelay(pdMS_TO_TICKS(100));
  Wire.beginTransmission(GPS_WR);
  Wire.write(d, n);
  return Wire.endTransmission() == 0;
}

// Turn off GLL, GSA, GSV and VTG ($PAIR062,<type>,0): the parser only needs GGA + RMC,
// and it cuts the data ~86% so the module's buffer has far more slack.
// The setting lives in the module's backup RAM; '$PAIR062,-1,1' or a long power-off undoes it.
bool gpsTrimSentences() {
  if (gpsCfgWrites >= GPS_CFG_MAX_WRITES) return false;
  char msg[96];
  size_t n = 0;
  static const int types[] = {1, 2, 3, 5};
  for (int t : types) {
    char body[24];
    snprintf(body, sizeof(body), "PAIR062,%d,0", t);
    uint8_t cs = 0;
    for (const char* q = body; *q; q++) cs ^= (uint8_t)*q;
    n += snprintf(msg + n, sizeof(msg) - n, "$%s*%02X\r\n", body, cs);
  }
  bool ok = false;
  for (int tries = 0; tries < 3 && !ok && gpsCfgWrites < GPS_CFG_MAX_WRITES; tries++) {
    gpsCfgWrites++;
#if GPS_USE_UART
    ok = Serial1.write((const uint8_t*)msg, n) == n;
#else
    ok = gpsWriteBytes((const uint8_t*)msg, n);
#endif
    Serial.printf("[gps] sentence trim (GGA+RMC only) sent: %s, try %d/3\n", ok ? "ok" : "failed",
                  tries + 1);
  }
  return ok;
}

// Reset the module and wait for it to answer, up to 3 tries. Takes busLock itself.
void gpsRecover(const char* why) {
  BusGuard g;
  Serial.printf("[gps] %s: resetting the module\n", why);
  bool ready = false;
  uint32_t took = 0;
  for (int attempt = 1; attempt <= 3 && !ready; attempt++) {
    gpsResetPulse();
#if GPS_USE_UART
    vTaskDelay(pdMS_TO_TICKS(500));
    ready = true;
#else
    ready = gpsAwaitReady(took);
    if (ready) Serial.printf("[gps] module answered %u ms after reset\n", (unsigned)took);
    else Serial.printf("[gps] no answer after reset (try %d/3)\n", attempt);
#endif
  }
#if GPS_TRIM_AT_RESET
  if (ready) gpsTrimSentences();
#endif
  uint32_t now = millis();
  gpsLastResetMs = now;
  gpsLastDataMs = now;  // give it a fresh 30 s
  gpsErrStreak = 0;
  portENTER_CRITICAL(&mux);
  shared.gpsResets++;
  shared.gpsReadyMs = ready ? took + 1 : 0;
  portEXIT_CRITICAL(&mux);
}

void readPmu() {
  if (!pmuOk_) return;
  pmu.getIrqStatus();
  bool pressed = pmu.isPekeyShortPressIrq();
  if (pressed) pmu.clearIrqStatus();
  int pct = pmu.isBatteryConnect() ? pmu.getBatteryPercent() : -1;
  bool chg = pmu.isCharging(), usb = pmu.isVbusIn();
  portENTER_CRITICAL(&mux);
  if (pressed) shared.pwrShortPresses++;
  shared.battPct = pct;
  shared.charging = chg;
  shared.usb = usb;
  portEXIT_CRITICAL(&mux);
}

void IRAM_ATTR onTouchIrq() { touchIrq = true; }

// Returns true while a finger is on the glass.
bool readTouch(uint32_t now) {
  if (!touchOk_) return false;
  bool down = false;
  int16_t x = 0, y = 0;
  if (touchIrq) {
    touchIrq = false;
    int16_t xs[2], ys[2];
    if (touch.getPoint(xs, ys, 1) > 0) {
      down = true;
      x = xs[0]; y = ys[0];
#if TOUCH_SWAP_XY
      int16_t t = x; x = y; y = t;
#endif
#if TOUCH_INVERT_X
      x = LCD_W - 1 - x;
#endif
#if TOUCH_INVERT_Y
      y = LCD_H - 1 - y;
#endif
#if DISPLAY_FLIP_180
      x = LCD_W - 1 - x;
      y = LCD_H - 1 - y;
#endif
    }
  }
  Swipe s = swipe.update(down, x, y, now);
  if (down || s != Swipe::None) {
    portENTER_CRITICAL(&mux);
    if (down) shared.lastTouchMs = now;
    if (s == Swipe::Left) shared.swipesLeft++;
    if (s == Swipe::Right) shared.swipesRight++;
    if (s == Swipe::Up) shared.swipesUp++;
    if (s == Swipe::Down) shared.swipesDown++;
    if (s == Swipe::Tap) shared.taps++;
    portEXIT_CRITICAL(&mux);
  }
  return swipe.active();
}

void sensorTask(void*) {
  uint32_t lastPmu = 0, lastGps = 0;
  gpsRecover("boot");  // an ESP32 reset never power-cycles the module; always start clean
  for (;;) {
    if (napping) { napIdle = true; vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    napIdle = false;
    uint32_t now = millis();
    if (gpsResetRequested) {
      gpsResetRequested = false;
      gpsRecover("requested");
      now = millis();
    } else if (gpsTrimRequested) {
      gpsTrimRequested = false;
      BusGuard g;
      gpsTrimSentences();
      now = millis();
    } else if (now - gpsLastDataMs > GPS_STALL_MS && now - gpsLastResetMs > GPS_RESET_GAP_MS) {
      char why[40];
      snprintf(why, sizeof(why), "no NMEA for %u s", (unsigned)((now - gpsLastDataMs) / 1000));
      gpsRecover(why);
      now = millis();
    }
    bool finger = readTouch(now);
    readAccel();
    readPwrLevel();
    if (now - lastPmu >= 100) { lastPmu = now; readPmu(); }
#if GPS_USE_UART
    (void)lastGps;
    (void)finger;
    size_t avail = Serial1.available();
    if (avail) {
      size_t n = 0;
      while (n < sizeof(gpsBuf) && Serial1.available()) gpsBuf[n++] = (uint8_t)Serial1.read();
      feedNmea(gpsBuf, n);
      gpsLastDataMs = millis();
      portENTER_CRITICAL(&mux);
      shared.gpsBytes += n;
      portEXIT_CRITICAL(&mux);
    }
#else
    // A GPS poll holds the bus 50-300 ms; never start one in the middle of a swipe.
    // While the link is failing, back off to one try every 2 s.
    uint32_t period = gpsErrStreak >= 3 ? 2000 : GPS_PERIOD_MS;
    if (!finger && now - lastGps >= period) { lastGps = now; gpsPoll(nullptr); }
#endif
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

namespace board {

bool begin() {
  busLock = xSemaphoreCreateMutex();
  Wire.setBufferSize(GPS_CHUNK + 16);  // must precede begin()
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C0_HZ);
  Wire.setTimeOut(1000);  // LC76G clock-stretches ~200 ms; the default 50 ms would fail

  // The AXP2101 occasionally misses the first read right after a reset: retry a few times.
  for (int i = 0; i < 5 && !pmuOk_; i++) {
    pmuOk_ = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
    if (!pmuOk_) delay(50);
  }
  if (pmuOk_) {
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
    pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);  // hold PWR 6 s = hard off
    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();
    pmu.enableVbusVoltageMeasure();
  }

  imuOk_ = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
  if (imuOk_) {
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_2G, SensorQMI8658::ACC_ODR_125Hz,
                            SensorQMI8658::LPF_MODE_0);
    imu.enableAccelerometer();
  }

  // Touch init mirrors Waveshare's 10_Touch_CST9217 example.
  touch.setPins(PIN_TP_RST, PIN_TP_INT);
  touchOk_ = touch.begin(Wire, CST92XX_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
  if (touchOk_) {
    pinMode(PIN_TP_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_TP_INT), onTouchIrq, FALLING);
  }

#if GPS_USE_UART
  Serial1.setRxBufferSize(4096);  // must precede begin(); ~3 s of default NMEA
  Serial1.begin(GPS_UART_BAUD, SERIAL_8N1, GPS_UART_RX, GPS_UART_TX);
#endif

  xTaskCreatePinnedToCore(sensorTask, "sensors", 6144, nullptr, 2, nullptr, 0);
  return pmuOk_ && imuOk_;
}

void snapshot(BoardSnapshot& out) {
  portENTER_CRITICAL(&mux);
  out = shared;
  portEXIT_CRITICAL(&mux);
}

void setNapping(bool n) {
  napping = n;
  if (!n) return;
  // Don't let light sleep freeze an I2C transfer halfway: wait for the sensor task to park.
  uint32_t t0 = millis();
  while (!napIdle && millis() - t0 < 1000) delay(5);
}

void pollPowerKey() {
  BusGuard g;
  readPmu();
}

void powerOff() {
  BusGuard g;
  if (pmuOk_) pmu.shutdown();
}

bool imuOk() { return imuOk_; }
bool pmuOk() { return pmuOk_; }
bool touchOk() { return touchOk_; }

void gpsProbe(Print& out) {
#if GPS_USE_UART
  out.println("GPS is in UART mode (GPS_USE_UART 1); see gps bytes in 'info'.");
  return;
#endif
  out.println("GPS poll (verbose):");
  gpsPoll(&out);
  BoardSnapshot b;
  snapshot(b);
  out.printf("polls=%u errs=%u unsticks=%u resets=%u bytes=%u\n", b.gpsPolls, b.gpsI2cErrs, b.gpsUnsticks,
             b.gpsResets, b.gpsBytes);
}

void gpsReset() { gpsResetRequested = true; }
void gpsTrim() { gpsTrimRequested = true; }

void gpsUartListen(Print& out) {
#if GPS_USE_UART
  out.println("Already in UART mode; 'info' shows gpsBytes climbing if NMEA is arriving.");
  return;
#endif
  static const int bauds[] = {115200, 9600};
  static const int rxPins[] = {18, 17};
  for (int rx : rxPins)
    for (int b : bauds) {
      Serial1.begin(b, SERIAL_8N1, rx, -1);
      delay(50);
      while (Serial1.available()) Serial1.read();
      uint32_t t0 = millis();
      size_t n = 0;
      char sample[121];
      size_t sn = 0;
      while (millis() - t0 < 1500) {
        while (Serial1.available()) {
          int c = Serial1.read();
          n++;
          if (sn < 120) sample[sn++] = (c >= 32 && c < 127) || c == '\n' ? (char)c : '.';
        }
        delay(5);
      }
      sample[sn] = 0;
      out.printf("UART RX=GPIO%d @%d: %u bytes  %s\n", rx, b, (unsigned)n, sample);
      Serial1.end();
    }
}

void i2cScan(Print& out) {
  BusGuard g;
  int found = 0;
  out.print("I2C0 (SDA 15, SCL 14):");
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      out.printf(" 0x%02X", a);
      found++;
    }
  }
  out.printf("  (%d found; expect 0x20 TCA9554, 0x34 AXP2101, 0x50/0x54 LC76G, 0x51 RTC, 0x5A touch, 0x6B IMU)\n", found);
}

}  // namespace board
