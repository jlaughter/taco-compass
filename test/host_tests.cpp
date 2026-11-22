// Host-side tests for the hardware-independent parts of TacoCompass.
//   make -C test
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

#include "../TacoCompass/geo.h"
#include "../TacoCompass/nmea.h"
#include "../TacoCompass/heading.h"
#include "../TacoCompass/buttons.h"
#include "../TacoCompass/decl_grid.h"
#include "../TacoCompass/game.h"
#include "fixture_db.h"  // generated from fixture data by build_db.py in the Makefile

static int fails = 0;
#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (!(cond)) {                                        \
      fails++;                                            \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);         \
      printf(__VA_ARGS__);                                \
      printf("\n");                                       \
    }                                                     \
  } while (0)

static bool near(double a, double b, double tol) { return fabs(a - b) <= tol; }
static float angdiff(float a, float b) { return fabsf(wrap180(a - b)); }

// ---------------------------------------------------------------- geo
static void test_geo() {
  // Atlanta -> Nashville, ~346 km, bearing ~322 (known reference values)
  float d = geo_distance_m(33.7490, -84.3880, 36.1627, -86.7816);
  CHECK(near(d, 346e3, 2e3), "ATL-BNA distance %f", d);
  float b = geo_bearing_deg(33.7490, -84.3880, 36.1627, -86.7816);
  CHECK(near(b, 321.5, 1.5), "ATL-BNA bearing %f", b);
  // Cardinal sanity
  CHECK(near(geo_bearing_deg(0, 0, 1, 0), 0, 0.01), "north");
  CHECK(near(geo_bearing_deg(0, 0, 0, 1), 90, 0.01), "east");
  CHECK(near(geo_bearing_deg(0, 0, -1, 0), 180, 0.01), "south");
  CHECK(near(geo_bearing_deg(0, 0, 0, -1), 270, 0.01), "west");
  // 1 degree of latitude ~ 111.2 km
  CHECK(near(geo_distance_m(10, 20, 11, 20), 111195, 50), "1 deg lat");
  CHECK(near(wrap180(190), -170, 1e-4) && near(wrap360(-10), 350, 1e-4), "wrap");

  // Nearest-N against brute force on random points, incl. high latitude.
  std::mt19937 rng(42);
  std::uniform_real_distribution<double> la(-60, 70), lo(-180, 180), jit(-0.5, 0.5);
  static PlaceLoc pts[3000];
  for (auto& p : pts) {
    p.lat_e6 = (int32_t)(la(rng) * 1e6);
    p.lon_e6 = (int32_t)(lo(rng) * 1e6);
    p.label_ofs = 0;
  }
  for (int trial = 0; trial < 200; trial++) {
    const PlaceLoc& c = pts[rng() % 3000];
    double qlat = c.lat_e6 * 1e-6 + jit(rng), qlon = c.lon_e6 * 1e-6 + jit(rng);
    if (qlon > 180) qlon -= 360;
    if (qlon < -180) qlon += 360;
    Nearby got[5];
    size_t n = geo_nearest(pts, 3000, qlat, qlon, got, 5);
    CHECK(n == 5, "n=%zu", n);
    // brute force exact
    float best[5] = {1e30f, 1e30f, 1e30f, 1e30f, 1e30f};
    for (auto& p : pts) {
      float dd = geo_distance_m(qlat, qlon, p.lat_e6 * 1e-6, p.lon_e6 * 1e-6);
      for (int k = 0; k < 5; k++)
        if (dd < best[k]) {
          for (int m = 4; m > k; m--) best[m] = best[m - 1];
          best[k] = dd;
          break;
        }
    }
    CHECK(near(got[0].meters, best[0], 0.5), "nearest #1 %f vs %f", got[0].meters, best[0]);
    for (int k = 1; k < 5; k++) CHECK(got[k].meters >= got[k - 1].meters, "sorted");
  }
  // Antimeridian: query at 179.99 should find a point at -179.99
  PlaceLoc am[2] = {{0, -179990000, 0}, {0, 170000000, 0}};
  Nearby g[1];
  geo_nearest(am, 2, 0, 179.99, g, 1);
  CHECK(g[0].index == 0 && g[0].meters < 3000, "antimeridian %u %f", g[0].index, g[0].meters);
  PlaceLoc am2[2] = {{0, 179990000, 0}, {0, -170000000, 0}};
  geo_nearest(am2, 2, 0, -179.99, g, 1);
  CHECK(g[0].index == 0 && g[0].meters < 3000, "antimeridian rev %u %f", g[0].index, g[0].meters);
  // k larger than n
  CHECK(geo_nearest(am, 2, 0, 0, g, 1) == 1, "k<n");
  Nearby g5[5];
  CHECK(geo_nearest(am, 2, 0, 0, g5, 5) == 2, "k>n");
  CHECK(geo_nearest(am, 0, 0, 0, g5, 5) == 0, "empty");
}

static void test_parse() {
  double a = 0, b = 0;
  CHECK(geo_parse_latlon("34.2073,-84.1402", a, b) && near(a, 34.2073, 1e-9) && near(b, -84.1402, 1e-9), "basic");
  CHECK(geo_parse_latlon(" 34.2073, -84.1402 ", a, b), "spaces");
  CHECK(geo_parse_latlon("34.2073 -84.1402", a, b) && near(b, -84.1402, 1e-9), "space separated");
  CHECK(geo_parse_latlon("-33.8688,151.2093", a, b) && a < 0 && b > 0, "southern/eastern");
  CHECK(!geo_parse_latlon("34.2073", a, b), "one number");
  CHECK(!geo_parse_latlon("34.2073,", a, b), "dangling comma");
  CHECK(!geo_parse_latlon("91,10", a, b), "lat range");
  CHECK(!geo_parse_latlon("10,181", a, b), "lon range");
  CHECK(!geo_parse_latlon("0,0", a, b), "null island");
  CHECK(!geo_parse_latlon("34.2,-84.1 x", a, b), "trailing junk");
  CHECK(!geo_parse_latlon("nan,5", a, b), "nan");
  CHECK(!geo_parse_latlon("abc", a, b) && !geo_parse_latlon("", a, b), "garbage");
  a = 1; b = 2;
  geo_parse_latlon("bad", a, b);
  CHECK(a == 1 && b == 2, "outputs untouched on failure");
}

static void test_declination() {
  auto D = [](double lat, double lon) {
    return geo_declination_deg(DECL_GRID, DECL_LAT0, DECL_LON0, DECL_STEP, DECL_ROWS, DECL_COLS,
                               lat, lon);
  };
  // Reference values from WMM2025 at late 2026 (pygeomag, exact location):
  CHECK(near(D(34.2, -84.14), -5.84, 0.4), "Cumming GA %f", D(34.2, -84.14));
  CHECK(near(D(37.77, -122.42), 12.82, 0.4), "SF %f", D(37.77, -122.42));
  CHECK(near(D(44.8, -68.8), -15.12, 0.5), "Bangor %f", D(44.8, -68.8));
  CHECK(near(D(61.2, -149.9), 13.96, 0.6), "Anchorage %f", D(61.2, -149.9));
  // out of range clamps rather than reading garbage
  float x = D(89.9, 0);
  CHECK(x > -180 && x < 180, "clamp");
}

// ---------------------------------------------------------------- nmea
static void feedStr(NmeaParser& p, GpsFix& f, const char* s) {
  for (; *s; s++) p.feed(*s, f);
}

static void test_nmea() {
  NmeaParser p;
  GpsFix f;
  // Classic reference sentences (checksums are correct)
  feedStr(p, f, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n");
  CHECK(f.valid, "rmc valid");
  CHECK(near(f.lat, 48.1173, 1e-4) && near(f.lon, 11.516667, 1e-4), "rmc pos %f %f", f.lat, f.lon);
  CHECK(near(f.speed_mps, 22.4 * 0.514444, 1e-3) && near(f.course_deg, 84.4, 1e-3), "rmc spd");
  feedStr(p, f, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
  CHECK(f.sats == 8 && near(f.hdop, 0.9, 1e-3), "gga sats %d", f.sats);
  CHECK(f.alt_valid && near(f.alt_m, 545.4, 1e-3), "gga altitude %f", f.alt_m);

  // Bad checksum ignored
  GpsFix g;
  feedStr(p, g, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*00\r\n");
  CHECK(!g.valid && p.badChecksums == 1, "bad checksum");

  // Build our own GN sentences with computed checksums, western/southern hemispheres.
  auto mk = [](const char* body, char* out) {
    uint8_t c = 0;
    for (const char* q = body; *q; q++) c ^= (uint8_t)*q;
    sprintf(out, "$%s*%02X\r\n", body, c);
  };
  char s[128];
  mk("GNRMC,051500.000,A,3412.3456,N,08408.4000,W,0.12,,091126,,,A,V", s);
  GpsFix h;
  feedStr(p, h, s);
  CHECK(h.valid && near(h.lat, 34.205760, 1e-5) && near(h.lon, -84.14, 1e-5), "GN W %f %f", h.lat,
        h.lon);
  CHECK(!h.course_valid, "empty course");
  mk("GNRMC,051500.000,A,3352.000,S,15112.000,E,0,0,091126,,,A,V", s);
  feedStr(p, h, s);
  CHECK(near(h.lat, -33.866667, 1e-5), "south");
  // Lost fix
  mk("GNRMC,051501.000,V,,,,,,,091126,,,N,V", s);
  feedStr(p, h, s);
  CHECK(!h.valid, "status V clears fix");
  mk("GNGGA,051502.000,,,,,0,03,,,M,,M,,", s);
  feedStr(p, h, s);
  CHECK(!h.valid && h.sats == 3, "gga no fix, sats %d", h.sats);
  // Garbage / partial / overlong lines don't crash or validate
  feedStr(p, h, "$GNRMC,12\r\nnoise$$$*ZZ\r\n");
  std::string longline = "$" + std::string(300, 'A') + "*00\r\n";
  feedStr(p, h, longline.c_str());
  CHECK(!h.valid, "garbage");
  // Sentences split across chunk boundaries (as the I2C reads will deliver them)
  mk("GNRMC,051503.000,A,3412.3456,N,08408.4000,W,3.50,271.0,091126,,,A,V", s);
  GpsFix k;
  size_t L = strlen(s);
  for (size_t i = 0; i < L; i++) p.feed(s[i], k);
  CHECK(k.valid && near(k.course_deg, 271.0, 1e-3) && near(k.speed_mps, 3.5 * 0.514444, 1e-3),
        "split");
}

// ---------------------------------------------------------------- heading
// Build what the sensors would read for a board with a given yaw/pitch/roll in a field
// with given inclination, then check tilt_heading recovers the yaw.
struct M3 { float m[3][3]; };
static M3 rotZ(float d) { float r = d * M_PI / 180, c = cosf(r), s = sinf(r); return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}}; }
static M3 rotX(float d) { float r = d * M_PI / 180, c = cosf(r), s = sinf(r); return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}}; }
static M3 rotY(float d) { float r = d * M_PI / 180, c = cosf(r), s = sinf(r); return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}}; }
static M3 mul(const M3& a, const M3& b) {
  M3 o{};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      for (int k = 0; k < 3; k++) o.m[i][j] += a.m[i][k] * b.m[k][j];
  return o;
}
static Vec3 tmul(const M3& a, Vec3 v) {  // transpose(a) * v : world -> body
  return {a.m[0][0] * v.x + a.m[1][0] * v.y + a.m[2][0] * v.z,
          a.m[0][1] * v.x + a.m[1][1] * v.y + a.m[2][1] * v.z,
          a.m[0][2] * v.x + a.m[1][2] * v.y + a.m[2][2] * v.z};
}

static void test_heading() {
  // World frame ENU-ish: X = east, Y = north, Z = up. Board lying flat, +Y forward.
  // Yaw (heading) is clockwise from north = rotation about Z by -heading.
  for (float incl : {0.f, 35.f, 63.f, 75.f}) {  // field dip: equator .. Georgia .. Alaska
    float Bh = 25 * cosf(incl * M_PI / 180), Bz = -25 * sinf(incl * M_PI / 180);
    Vec3 Bworld = {0, Bh, Bz};       // points north and down
    Vec3 Aworld = {0, 0, 1};         // accelerometer reads +1 g up
    for (float hdg = 0; hdg < 360; hdg += 15)
      for (float pitch : {-40.f, 0.f, 25.f})
        for (float roll : {-30.f, 0.f, 45.f}) {
          M3 R = mul(rotZ(-hdg), mul(rotX(pitch), rotY(roll)));  // body -> world
          Vec3 a = tmul(R, Aworld), m = tmul(R, Bworld);
          float h;
          bool ok = tilt_heading(a, m, h);
          CHECK(ok && angdiff(h, hdg) < 0.05f, "incl %.0f hdg %.0f p %.0f r %.0f -> %f", incl, hdg,
                pitch, roll, h);
        }
  }
  float h;
  CHECK(!tilt_heading({0, 0, 0}, {1, 1, 1}, h), "freefall");
  CHECK(!tilt_heading({0, 0, 1}, {0, 0, -40}, h), "field parallel to gravity");

  // Calibration: synthesize a hard/soft-iron distorted sphere and recover it.
  MagCalibrator c;
  c.reset();
  std::mt19937 rng(7);
  std::normal_distribution<float> nd(0, 1);
  const float off[3] = {-31.5f, 12.0f, 48.0f}, sc[3] = {1.2f, 0.9f, 1.05f};
  for (int i = 0; i < 3000; i++) {
    Vec3 u = {nd(rng), nd(rng), nd(rng)};
    float n = sqrtf(u.x * u.x + u.y * u.y + u.z * u.z);
    Vec3 b = {u.x / n * 50, u.y / n * 50, u.z / n * 50};
    c.add({b.x * sc[0] + off[0], b.y * sc[1] + off[1], b.z * sc[2] + off[2]});
  }
  MagCal mc;
  CHECK(c.finish(mc), "cal finish");
  CHECK(near(mc.offset[0], off[0], 1.5) && near(mc.offset[1], off[1], 1.5) &&
            near(mc.offset[2], off[2], 1.5), "offsets %f %f %f", mc.offset[0], mc.offset[1], mc.offset[2]);
  // corrected axes should have equal spans
  float r0 = 50 * sc[0] * mc.scale[0], r1 = 50 * sc[1] * mc.scale[1], r2 = 50 * sc[2] * mc.scale[2];
  CHECK(near(r0, r1, 2) && near(r1, r2, 2), "scales %f %f %f", r0, r1, r2);
  CHECK(c.coverage() > 0.95f, "coverage %f", c.coverage());

  // End-to-end: distorted readings + calibration -> correct heading.
  for (float hdg = 0; hdg < 360; hdg += 30) {
    M3 R = mul(rotZ(-hdg), rotX(10));
    Vec3 m = tmul(R, {0, 22, -42}), a = tmul(R, {0, 0, 1});
    Vec3 raw = {m.x * sc[0] + off[0], m.y * sc[1] + off[1], m.z * sc[2] + off[2]};
    CHECK(tilt_heading(a, mc.apply(raw), h) && angdiff(h, hdg) < 3.0f, "e2e %f vs %f", h, hdg);
  }

  // A lazy wave in one plane shouldn't pass.
  MagCalibrator lazy;
  lazy.reset();
  for (int i = 0; i < 500; i++) {
    float t = i * 0.05f;
    lazy.add({30 * cosf(t), 30 * sinf(t), 5});
  }
  MagCal junk;
  CHECK(!lazy.finish(junk), "flat wave rejected");
  CHECK(lazy.coverage() < 0.7f, "flat coverage %f", lazy.coverage());

  // Smoother wraps correctly across north
  AngleSmoother s(0.5f);
  s.update(350);
  for (int i = 0; i < 30; i++) s.update(10);
  CHECK(angdiff(s.value(), 10) < 0.1f, "smooth wrap %f", s.value());
  AngleSmoother s2(0.5f);
  s2.update(350);
  float mid = s2.update(10);
  CHECK(angdiff(mid, 0) < 0.5f, "midpoint through north %f", mid);
}

static void test_level() {
  float r, p, t;
  CHECK(level_angles({0, 0, 1}, r, p, t) && near(r, 0, 1e-4) && near(p, 0, 1e-4) && near(t, 0, 1e-3), "flat");
  // Raise the 3 o'clock edge by 10 deg: gravity reaction gains a +X component.
  float d = 10 * M_PI / 180;
  CHECK(level_angles({sinf(d), 0, cosf(d)}, r, p, t) && near(r, 10, 1e-3) && near(p, 0, 1e-3) && near(t, 10, 1e-3), "roll %f %f", r, t);
  CHECK(level_angles({0, -sinf(d), cosf(d)}, r, p, t) && near(p, -10, 1e-3) && near(r, 0, 1e-3), "pitch %f", p);
  // Same answers whatever the magnitude (accelerometer scale error)
  CHECK(level_angles({0.2f * sinf(d) * 1.1f, 0, cosf(d) * 1.1f}, r, p, t) && r > 0, "scale");
  CHECK(!level_angles({0, 0, 0}, r, p, t), "freefall");
  CHECK(level_angles({0, 0, -1}, r, p, t) && near(t, 180, 1e-3), "upside down %f", t);
  CHECK(!strcmp(cardinal16(0), "N") && !strcmp(cardinal16(11.2f), "N") && !strcmp(cardinal16(11.3f), "NNE") &&
        !strcmp(cardinal16(247), "WSW") && !strcmp(cardinal16(359), "N") && !strcmp(cardinal16(-90), "W") &&
        !strcmp(cardinal16(180), "S"), "cardinals");
}

// ---------------------------------------------------------------- buttons
static void test_buttons() {
  Button b(2500, 30);
  uint32_t t = 0;
  int shorts = 0, longs = 0;
  auto run = [&](bool p, uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += 5) {
      BtnEvent e = b.update(p, t);
      t += 5;
      shorts += e == BtnEvent::Short;
      longs += e == BtnEvent::Long;
    }
  };
  run(false, 100);
  run(true, 200); run(false, 100);
  CHECK(shorts == 1 && longs == 0, "short %d %d", shorts, longs);
  run(true, 3000); run(false, 100);
  CHECK(shorts == 1 && longs == 1, "long then release isn't short %d %d", shorts, longs);
  // Bounce shorter than debounce ignored
  for (int i = 0; i < 10; i++) { run(true, 10); run(false, 10); }
  run(false, 100);
  CHECK(shorts == 1, "bounce %d", shorts);
  // millis() rollover
  Button r(2500, 30);
  t = 0xFFFFFF00u;
  for (int i = 0; i < 40; i++) r.update(true, t += 5);
  BtnEvent e = BtnEvent::None;
  for (int i = 0; i < 40; i++) { BtnEvent x = r.update(false, t += 5); if (x != BtnEvent::None) e = x; }
  CHECK(e == BtnEvent::Short, "rollover");
}

static void test_swipe() {
  // Simulate a touch panel reporting every ~12 ms while touched, task ticking every 10 ms.
  auto gesture = [](SwipeDetector& d, int x0, int y0, int x1, int y1, uint32_t durMs, uint32_t& t) {
    Swipe result = Swipe::None;
    for (uint32_t e = 0; e <= durMs; e += 10, t += 10) {
      float f = durMs ? (float)e / durMs : 1;
      bool report = (e % 12) < 10;  // some ticks have no fresh sample
      Swipe r = d.update(report, (int16_t)(x0 + (x1 - x0) * f), (int16_t)(y0 + (y1 - y0) * f), t);
      if (r != Swipe::None) result = r;
    }
    for (int i = 0; i < 30; i++, t += 10) {  // finger up
      Swipe r = d.update(false, 0, 0, t);
      if (r != Swipe::None) result = r;
    }
    return result;
  };
  SwipeDetector d(70);
  uint32_t t = 1000;
  CHECK(gesture(d, 350, 230, 120, 240, 250, t) == Swipe::Left, "swipe left");
  CHECK(gesture(d, 120, 230, 360, 210, 250, t) == Swipe::Right, "swipe right");
  CHECK(gesture(d, 230, 360, 240, 110, 250, t) == Swipe::Up, "swipe up");
  CHECK(gesture(d, 240, 100, 225, 350, 250, t) == Swipe::Down, "swipe down");
  CHECK(gesture(d, 233, 233, 236, 231, 120, t) == Swipe::Tap, "tap recognized");
  CHECK(gesture(d, 233, 233, 236, 231, 900, t) == Swipe::None, "long press is not a tap");
  CHECK(gesture(d, 120, 120, 330, 330, 300, t) == Swipe::None, "diagonal ignored");
  CHECK(gesture(d, 300, 230, 250, 230, 200, t) == Swipe::None, "too short");
  CHECK(gesture(d, 233, 300, 233, 250, 200, t) == Swipe::None, "too short vertical");
  CHECK(gesture(d, 380, 230, 100, 230, 2500, t) == Swipe::None, "too slow (drag)");
  // A hiccup in reports shorter than the release window must not split the swipe
  SwipeDetector h(70);
  uint32_t u = 0;
  Swipe r = Swipe::None;
  auto keep = [&](Swipe x) { if (x != Swipe::None) r = x; };
  for (int y = 350; y > 250; y -= 10) keep(h.update(true, 230, (int16_t)y, u += 10));
  u += 60;  // 60 ms with no samples (bus busy)
  keep(h.update(false, 0, 0, u));
  for (int y = 250; y > 120; y -= 10) keep(h.update(true, 230, (int16_t)y, u += 10));
  for (int i = 0; i < 20; i++) keep(h.update(false, 0, 0, u += 10));
  CHECK(r == Swipe::Up, "gap-tolerant swipe %d", (int)r);

  // Button::cancel swallows the release of the current press
  Button b(2500, 30);
  uint32_t m = 0;
  int shorts = 0;
  for (int i = 0; i < 20; i++) shorts += b.update(true, m += 5) == BtnEvent::Short;
  b.cancel();
  for (int i = 0; i < 20; i++) shorts += b.update(false, m += 5) == BtnEvent::Short;
  for (int i = 0; i < 20; i++) shorts += b.update(true, m += 5) == BtnEvent::Short;
  for (int i = 0; i < 20; i++) shorts += b.update(false, m += 5) == BtnEvent::Short;
  CHECK(shorts == 1, "cancel only affects one press (%d)", shorts);
}

// ---------------------------------------------------------------- db
static void test_db() {
  // fixture_db.h is built from fixtures/breweries.kmz (KML subset + bundled icon)
  CHECK(DB_COUNT == 5, "fixture count %d (dup merged, LineString skipped?)", DB_COUNT);
  CHECK(!strcmp(DB_TITLE, "Breweries") && !strcmp(DB_DEVICE_NAME, "Brew Compass"), "metadata");
  CHECK(DB_THEME == 2 && DB_HAS_ICON == 1, "icon theme from KMZ");
  CHECK(DB_ICON_L_W <= 96 && DB_ICON_L_H <= 96 && DB_ICON_S_W <= 28 && DB_ICON_S_H <= 28 &&
            (DB_ICON_L_W == 96 || DB_ICON_L_H == 96), "icon sizes %dx%d %dx%d", DB_ICON_L_W, DB_ICON_L_H,
        DB_ICON_S_W, DB_ICON_S_H);
  for (int i = 0; i < DB_COUNT; i++) {
    const char* s = DB_LABELS + DB_LOCS[i].label_ofs;
    int bars = 0;
    for (const char* p = s; *p; p++) bars += *p == '|';
    CHECK(bars == 2, "label %d '%s'", i, s);
  }
  Nearby n[5];
  size_t k = geo_nearest(DB_LOCS, DB_COUNT, 34.0, -84.0, n, 5);
  CHECK(k == 5, "k");
  char name[36], street[36], city[36];
  split_label(DB_LABELS + DB_LOCS[n[0].index].label_ofs, name, street, city, sizeof(name));
  CHECK(!strcmp(name, "Fixture Brewing Co") && !strcmp(street, "101 Fixture Ave NE Suite 2") &&
            !strcmp(city, "Testville, GA"), "nearest '%s' '%s' '%s'", name, street, city);
  // lon/lat order: the place at lon -84.05, lat 34.05 must be ~7 km NW, not in another hemisphere
  bool foundMock = false;
  for (size_t i = 0; i < k; i++) {
    split_label(DB_LABELS + DB_LOCS[n[i].index].label_ofs, name, street, city, sizeof(name));
    if (!strcmp(name, "Mock Ale Works")) {
      foundMock = true;
      CHECK(near(n[i].meters, 7200, 300) && !strcmp(street, "202 Pretend Pkwy") && !strcmp(city, "Mocktown, GA"),
            "mock %f '%s' '%s'", n[i].meters, street, city);
    }
    if (!strcmp(street, "303 Same Name St")) CHECK(name[0] == 0, "name equal to title is blanked");
  }
  CHECK(foundMock, "description-address placemark present");
}

static void test_text() {
  char a[12], b[12], c[12];
  split_label("Name|Street|City, ST", a, b, c, sizeof(a));
  CHECK(!strcmp(a, "Name") && !strcmp(b, "Street") && !strcmp(c, "City, ST"), "split");
  split_label("|1 Main St|", a, b, c, sizeof(a));
  CHECK(!a[0] && !strcmp(b, "1 Main St") && !c[0], "empties");
  split_label("A very long place name|x|y", a, b, c, sizeof(a));
  CHECK(strlen(a) == 11 && !strcmp(b, "x"), "truncates safely");
  split_label("legacy", a, b, c, sizeof(a));
  CHECK(!strcmp(a, "legacy") && !b[0] && !c[0], "no bars");

  char num[16], unit[12];
  struct { float m; bool metric; const char* num; const char* unit; } cases[] = {
      {0, false, "0", "feet"},          {137.2f, false, "450", "feet"},  {161.0f, false, "0.1", "miles"},
      {3862, false, "2.4", "miles"},    {16000, false, "9.9", "miles"},  {16100, false, "10", "miles"},
      {43933, false, "27", "miles"},    {0, true, "0", "meters"},        {847, true, "850", "meters"},
      {994, true, "990", "meters"},     {996, true, "1.0", "km"},        {3862, true, "3.9", "km"},
      {9949, true, "9.9", "km"},        {9951, true, "10", "km"},        {43933, true, "44", "km"},
  };
  for (auto& t : cases) {
    format_distance(t.m, t.metric, num, sizeof(num), unit, sizeof(unit));
    CHECK(!strcmp(num, t.num) && !strcmp(unit, t.unit), "%.1f m %s -> '%s %s' (want '%s %s')", t.m,
          t.metric ? "metric" : "freedom", num, unit, t.num, t.unit);
  }
}

static void test_game() {
  NachoGame g;
  g.begin(100);
  CHECK(g.state() == GameState::Idle, "game starts idle");
  uint32_t t = 1000;
  g.start(t, 30.0f, 12345);  // held at 30 deg: that's the new zero
  CHECK(g.playing() && g.lives() == 3 && g.score() == 0, "start resets");
  float mid = (NachoGame::TOP + NachoGame::BOTTOM) / 2;
  CHECK(fabsf(g.dogY() - mid) < 0.5f, "pup starts mid-field");

  // Tilt: +full tilt -> top, -full tilt -> bottom, zero -> middle.
  for (int i = 0; i < 60; i++) g.update(t += 16, 30.0f + NachoGame::FULL_TILT_DEG);
  CHECK(fabsf(g.dogY() - NachoGame::TOP) < 3, "full tilt up reaches the top (%.1f)", g.dogY());
  for (int i = 0; i < 60; i++) g.update(t += 16, 30.0f - 2 * NachoGame::FULL_TILT_DEG);  // beyond full: clamps
  CHECK(fabsf(g.dogY() - NachoGame::BOTTOM) < 3, "over-tilt down clamps to the bottom (%.1f)", g.dogY());
  for (int i = 0; i < 60; i++) g.update(t += 16, 30.0f);
  CHECK(fabsf(g.dogY() - mid) < 3, "back to zero -> middle");

  // Scoring: drop food right in front of the pup's mouth.
  NachoGame h;
  h.begin(0);
  h.start(t, 0, 7);
  float y = h.dogY();
  h.injectItem(FoodKind::Chip, NachoGame::DOG_X + 24, y);
  h.update(t += 16, 0);
  CHECK(h.score() == 5, "chip +5 (got %d)", h.score());
  h.injectItem(FoodKind::Taco, NachoGame::DOG_X + 24, y);
  h.update(t += 16, 0);
  h.injectItem(FoodKind::Churro, NachoGame::DOG_X + 24, y);
  h.update(t += 16, 0);
  CHECK(h.score() == 35, "taco +10, churro +20 (got %d)", h.score());
  // Food far away is not eaten.
  h.injectItem(FoodKind::Churro, NachoGame::DOG_X + 24, y + 120);
  h.update(t += 16, 0);
  CHECK(h.score() == 35, "food out of reach isn't eaten");

  // Burgers cost sombreros; the third ends the game and sets a new best.
  for (int k = 1; k <= 3; k++) {
    h.injectItem(FoodKind::Burger, NachoGame::DOG_X + 24, h.dogY());
    h.update(t += 16, 0);
    CHECK(h.lives() == 3 - k, "burger %d -> %d sombreros (got %d)", k, 3 - k, h.lives());
  }
  CHECK(h.state() == GameState::Over, "three burgers: game over");
  CHECK(h.best() == 35 && h.bestChanged() && h.view(t).newBest, "new best recorded");
  uint32_t before = h.score();
  h.update(t += 500, 0);
  CHECK((uint32_t)h.score() == before, "no scoring after game over");

  // BOOT ends a game without beating the best.
  NachoGame e;
  e.begin(500);
  e.start(t, 0, 9);
  e.end(t);
  CHECK(e.state() == GameState::Over && !e.bestChanged() && e.best() == 500, "manual end keeps best");

  // Levels: every 50 points; the field fills and speeds up, and nothing piles up past capacity.
  NachoGame r;
  r.begin(0);
  r.start(0, 0, 4242);
  uint32_t now = 0;
  int maxItems = 0;
  for (int i = 0; i < 60 * 120 && r.playing(); i++) {  // two minutes at 60 Hz, steering wildly
    float tilt = 25.0f * sinf(i * 0.013f);
    r.update(now += 16, tilt);
    if (r.itemCount() > maxItems) maxItems = r.itemCount();
    for (int k = 0; k < r.itemCount(); k++)
      CHECK(r.item(k).y >= NachoGame::TOP && r.item(k).y <= NachoGame::BOTTOM, "food spawns in the field");
  }
  CHECK(maxItems > 2 && maxItems <= NachoGame::MAX_ITEMS, "food appears and stays bounded (max %d)", maxItems);
  CHECK(r.level() == 1 + r.score() / 50, "level follows score");

  // A long pause (nap) doesn't teleport the food.
  NachoGame p;
  p.begin(0);
  p.start(0, 0, 3);
  p.injectItem(FoodKind::Chip, 400, NachoGame::TOP + 5);
  p.update(60000, 0);
  CHECK(p.item(0).x > 370, "dt is clamped after a pause (x=%d)", p.item(0).x);
}

int main() {
  test_geo();
  test_declination();
  test_parse();
  test_nmea();
  test_heading();
  test_level();
  test_buttons();
  test_swipe();
  test_db();
  test_text();
  test_game();
  if (fails) { printf("%d FAILED\n", fails); return 1; }
  printf("all host tests passed\n");
  return 0;
}
