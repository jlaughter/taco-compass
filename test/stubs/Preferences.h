#pragma once
#include "Arduino.h"
class Preferences {  // subset of arduino-esp32 Preferences
 public:
  bool begin(const char* name, bool readOnly = false, const char* partition_label = NULL);
  bool remove(const char* key);
  size_t putBool(const char* key, bool value);
  size_t putDouble(const char* key, double value);
  size_t putFloat(const char* key, float value);
  size_t putUChar(const char* key, uint8_t value);
  size_t putInt(const char* key, int32_t value);
  int32_t getInt(const char* key, int32_t defaultValue = 0);
  uint8_t getUChar(const char* key, uint8_t defaultValue = 0);
  float getFloat(const char* key, float defaultValue = NAN);
  size_t putBytes(const char* key, const void* value, size_t len);
  bool getBool(const char* key, bool defaultValue = false);
  double getDouble(const char* key, double defaultValue = NAN);
  size_t getBytesLength(const char* key);
  size_t getBytes(const char* key, void* buf, size_t maxLen);
};
