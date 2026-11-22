#include "nmea.h"
#include <stdlib.h>
#include <string.h>

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// "ddmm.mmmm" or "dddmm.mmmm" + hemisphere -> signed degrees
static bool parseCoord(const char* s, const char* hemi, double& out) {
  if (!s || !*s || !hemi || !*hemi) return false;
  double v = atof(s);
  int deg = (int)(v / 100);
  double minutes = v - deg * 100.0;
  out = deg + minutes / 60.0;
  if (*hemi == 'S' || *hemi == 'W') out = -out;
  return true;
}

bool NmeaParser::feed(char c, GpsFix& fix) {
  if (c == '$') { len_ = 0; buf_[len_++] = c; return false; }
  if (len_ == 0) return false;
  if (c == '\r' || c == '\n') {
    buf_[len_] = 0;
    bool r = parse(fix);
    len_ = 0;
    return r;
  }
  if (len_ < (int)sizeof(buf_) - 1) buf_[len_++] = c;
  else len_ = 0;  // overlong garbage, drop
  return false;
}

bool NmeaParser::parse(GpsFix& fix) {
  char* star = strchr(buf_, '*');
  if (!star || star[1] == 0 || star[2] == 0) return false;
  uint8_t sum = 0;
  for (char* p = buf_ + 1; p < star; p++) sum ^= (uint8_t)*p;
  int h = hexval(star[1]), l = hexval(star[2]);
  if (h < 0 || l < 0 || sum != (uint8_t)((h << 4) | l)) { badChecksums++; return false; }
  *star = 0;

  // Split on commas, keeping empty fields.
  char* f[24];
  int nf = 0;
  char* p = buf_ + 1;
  f[nf++] = p;
  while (*p && nf < 24) {
    if (*p == ',') { *p = 0; f[nf++] = p + 1; }
    p++;
  }
  if (strlen(f[0]) < 5) return false;
  const char* type = f[0] + 2;  // skip talker id

  if (strcmp(type, "RMC") == 0 && nf >= 9) {
    // 0 type,1 time,2 status,3 lat,4 N/S,5 lon,6 E/W,7 sog kn,8 cog
    bool ok = f[2][0] == 'A';
    double la, lo;
    if (ok && parseCoord(f[3], f[4], la) && parseCoord(f[5], f[6], lo)) {
      fix.lat = la; fix.lon = lo;
      fix.speed_mps = f[7][0] ? (float)(atof(f[7]) * 0.514444) : 0.f;
      fix.course_valid = f[8][0] != 0;
      if (fix.course_valid) fix.course_deg = (float)atof(f[8]);
      fix.valid = true;
      fix.updates++;
      return true;
    }
    fix.valid = false;
    return false;
  }
  if (strcmp(type, "GGA") == 0 && nf >= 9) {
    // 0 type,1 time,2 lat,3 N/S,4 lon,5 E/W,6 quality,7 sats,8 hdop
    int q = atoi(f[6]);
    fix.sats = atoi(f[7]);
    if (f[8][0]) fix.hdop = (float)atof(f[8]);
    if (nf >= 10) {
      fix.alt_valid = q > 0 && f[9][0] != 0;
      if (fix.alt_valid) fix.alt_m = (float)atof(f[9]);
    }
    double la, lo;
    if (q > 0 && parseCoord(f[2], f[3], la) && parseCoord(f[4], f[5], lo)) {
      fix.lat = la; fix.lon = lo;
      fix.valid = true;
      fix.updates++;
      return true;
    }
    if (q == 0) fix.valid = false;
    return false;
  }
  return false;
}
