// Pure math: distances, bearings, nearest-N search, magnetic declination.
// No Arduino dependencies so it can be unit-tested on a PC.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct PlaceLoc {
  int32_t lat_e6;      // degrees * 1e6
  int32_t lon_e6;
  uint32_t label_ofs;  // offset into label pool: "street|City, ST\0"
};

struct Nearby {
  uint32_t index;      // into the location table
  float meters;        // great-circle distance
  float bearing_deg;   // true bearing from you to it, 0..360
};

float geo_distance_m(double lat1, double lon1, double lat2, double lon2);
float geo_bearing_deg(double lat1, double lon1, double lat2, double lon2);

// Fills out[0..k-1] sorted nearest-first; returns how many were found (<= k).
size_t geo_nearest(const PlaceLoc* locs, size_t n, double lat, double lon,
                   Nearby* out, size_t k);

// Bilinear lookup in a regular lat/lon grid of declination (tenths of a degree).
float geo_declination_deg(const int16_t* grid, int lat0, int lon0, int step,
                          int rows, int cols, double lat, double lon);

// Parse "34.2073,-84.1402" (comma and/or spaces between). Rejects out-of-range
// values, trailing junk, and 0,0. Returns true on success.
bool geo_parse_latlon(const char* s, double& lat, double& lon);

// Distance as display text. Freedom Units: feet under 0.1 mi, then miles
// (one decimal under 10). Metric: meters under 1 km, then km.
void format_distance(float meters, bool metric, char* num, size_t numLen, char* unit, size_t unitLen);

// A place's display text. (Declared in a header, not the .ino: the Arduino IDE
// auto-inserts prototypes above any types defined inside the sketch.)
struct PlaceText { char name[36], street[36], city[36]; };

// Split a "name|street|city, ST" label into its three fields (each buffer n bytes).
void split_label(const char* label, char* name, char* street, char* city, size_t n);

// Normalize to [0,360)
float wrap360(float a);
// Normalize to (-180,180]
float wrap180(float a);
