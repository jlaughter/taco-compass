#include "geo.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

static const double kR = 6371008.8;  // mean Earth radius, m
static const double kDeg = M_PI / 180.0;

float wrap360(float a) {
  a = fmodf(a, 360.0f);
  if (a < 0) a += 360.0f;
  return a;
}

float wrap180(float a) {
  a = wrap360(a);
  if (a > 180.0f) a -= 360.0f;
  return a;
}

float geo_distance_m(double lat1, double lon1, double lat2, double lon2) {
  double p1 = lat1 * kDeg, p2 = lat2 * kDeg;
  double dp = (lat2 - lat1) * kDeg, dl = (lon2 - lon1) * kDeg;
  double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  return (float)(2 * kR * atan2(sqrt(a), sqrt(1 - a)));
}

float geo_bearing_deg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = lat1 * kDeg, p2 = lat2 * kDeg, dl = (lon2 - lon1) * kDeg;
  double y = sin(dl) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  return wrap360((float)(atan2(y, x) / kDeg));
}

size_t geo_nearest(const PlaceLoc* locs, size_t n, double lat, double lon,
                   Nearby* out, size_t k) {
  if (k == 0) return 0;
  // Rank with a cheap equirectangular metric (squared, in "degree" units),
  // then compute exact distance/bearing only for the winners.
  const double coslat = cos(lat * kDeg);
  double best[16];
  uint32_t bestIdx[16];
  if (k > 16) k = 16;
  size_t have = 0;

  for (size_t i = 0; i < n; i++) {
    double dLat = locs[i].lat_e6 * 1e-6 - lat;
    double dLon = locs[i].lon_e6 * 1e-6 - lon;
    if (dLon > 180) dLon -= 360;
    if (dLon < -180) dLon += 360;
    dLon *= coslat;
    double d2 = dLat * dLat + dLon * dLon;
    if (have < k || d2 < best[have - 1]) {
      size_t j = (have < k) ? have++ : have - 1;
      while (j > 0 && best[j - 1] > d2) {  // insertion sort, k is tiny
        best[j] = best[j - 1];
        bestIdx[j] = bestIdx[j - 1];
        j--;
      }
      best[j] = d2;
      bestIdx[j] = (uint32_t)i;
    }
  }

  for (size_t j = 0; j < have; j++) {
    const PlaceLoc& L = locs[bestIdx[j]];
    out[j].index = bestIdx[j];
    out[j].meters = geo_distance_m(lat, lon, L.lat_e6 * 1e-6, L.lon_e6 * 1e-6);
    out[j].bearing_deg = geo_bearing_deg(lat, lon, L.lat_e6 * 1e-6, L.lon_e6 * 1e-6);
  }
  // The equirectangular ranking can disagree with the true distance by a
  // hair at long range; re-sort the short list by the exact number.
  for (size_t a = 1; a < have; a++) {
    Nearby t = out[a];
    size_t b = a;
    while (b > 0 && out[b - 1].meters > t.meters) { out[b] = out[b - 1]; b--; }
    out[b] = t;
  }
  return have;
}

float geo_declination_deg(const int16_t* grid, int lat0, int lon0, int step,
                          int rows, int cols, double lat, double lon) {
  double fy = (lat - lat0) / step;
  double fx = (lon - lon0) / step;
  if (fy < 0) fy = 0;
  if (fy > rows - 1) fy = rows - 1;
  if (fx < 0) fx = 0;
  if (fx > cols - 1) fx = cols - 1;
  int y0 = (int)fy, x0 = (int)fx;
  int y1 = y0 + 1 < rows ? y0 + 1 : y0;
  int x1 = x0 + 1 < cols ? x0 + 1 : x0;
  double ty = fy - y0, tx = fx - x0;
  double v00 = grid[y0 * cols + x0], v01 = grid[y0 * cols + x1];
  double v10 = grid[y1 * cols + x0], v11 = grid[y1 * cols + x1];
  double v = (v00 * (1 - tx) + v01 * tx) * (1 - ty) + (v10 * (1 - tx) + v11 * tx) * ty;
  return (float)(v / 10.0);
}

bool geo_parse_latlon(const char* s, double& lat, double& lon) {
  char* end;
  while (*s == ' ') s++;
  double a = strtod(s, &end);
  if (end == s) return false;
  s = end;
  bool sep = false;
  while (*s == ' ' || *s == ',') { sep = true; s++; }
  if (!sep) return false;
  double b = strtod(s, &end);
  if (end == s) return false;
  s = end;
  while (*s == ' ') s++;
  if (*s) return false;
  if (!(a >= -90 && a <= 90 && b >= -180 && b <= 180)) return false;  // also rejects NaN
  if (a == 0 && b == 0) return false;
  lat = a; lon = b;
  return true;
}

void format_distance(float m, bool metric, char* num, size_t nn, char* unit, size_t un) {
  if (!metric) {
    float mi = m / 1609.344f;
    if (mi < 0.1f) {
      snprintf(num, nn, "%ld", lroundf(m * 3.28084f / 10.0f) * 10);
      snprintf(unit, un, "feet");
    } else if (mi < 9.95f) {
      snprintf(num, nn, "%.1f", mi);
      snprintf(unit, un, "miles");
    } else {
      snprintf(num, nn, "%ld", lroundf(mi));
      snprintf(unit, un, "miles");
    }
  } else if (m < 995.0f) {
    snprintf(num, nn, "%ld", lroundf(m / 10.0f) * 10);
    snprintf(unit, un, "meters");
  } else if (m < 9950.0f) {
    snprintf(num, nn, "%.1f", m / 1000.0f);
    snprintf(unit, un, "km");
  } else {
    snprintf(num, nn, "%ld", lroundf(m / 1000.0f));
    snprintf(unit, un, "km");
  }
}

void split_label(const char* s, char* name, char* street, char* city, size_t n) {
  char* out[3] = {name, street, city};
  for (int f = 0; f < 3; f++) {
    const char* bar = nullptr;
    if (f < 2)
      for (const char* p = s; *p; p++)
        if (*p == '|') { bar = p; break; }
    size_t len = 0;
    if (bar) len = (size_t)(bar - s);
    else while (s[len]) len++;
    snprintf(out[f], n, "%.*s", (int)len, s);
    s = bar ? bar + 1 : s + len;
  }
}
