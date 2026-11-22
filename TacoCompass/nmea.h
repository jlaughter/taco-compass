// Minimal NMEA 0183 parser for RMC + GGA (any talker: GP/GN/GL/GA/BD).
#pragma once
#include <stdint.h>
#include <stdbool.h>

struct GpsFix {
  bool valid = false;        // RMC status A and GGA quality > 0 (whichever seen last)
  double lat = 0, lon = 0;   // degrees
  float speed_mps = 0;
  float course_deg = 0;
  bool course_valid = false;
  int sats = 0;
  float hdop = 99;
  float alt_m = 0;           // GGA altitude above mean sea level
  bool alt_valid = false;
  uint32_t updates = 0;      // increments on each accepted position sentence
};

class NmeaParser {
 public:
  // Feed one character at a time. Returns true when a sentence updated the fix.
  bool feed(char c, GpsFix& fix);
  uint32_t badChecksums = 0;

 private:
  char buf_[96];
  int len_ = 0;
  bool parse(GpsFix& fix);
};
