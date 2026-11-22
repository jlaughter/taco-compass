// Signatures copied from GFX Library for Arduino 1.6.4 (the copy Waveshare ships)
#pragma once
#include "Arduino.h"
#include "gfxfont.h"
#define GFX_NOT_DEFINED -1
class Arduino_DataBus { public: virtual ~Arduino_DataBus() {} };
class Arduino_ESP32QSPI : public Arduino_DataBus {
 public:
  Arduino_ESP32QSPI(int8_t cs, int8_t sck, int8_t mosi, int8_t miso, int8_t quadwp, int8_t quadhd,
                    bool is_shared_interface = false);
};
class Arduino_G { public: virtual bool begin(int32_t speed = GFX_NOT_DEFINED) = 0; };
class Arduino_GFX : public Print, public Arduino_G {
 public:
  void fillScreen(uint16_t color);
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
  void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
  void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
  void setFont(const GFXfont* f = NULL);
  void setFont(const uint8_t* font);
  void getTextBounds(const char* string, int16_t x, int16_t y, int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h);
  void setCursor(int16_t x, int16_t y);
  void setTextColor(uint16_t c);
  size_t write(uint8_t) override;
};
class Arduino_TFT : public Arduino_GFX {
 public:
  virtual void displayOn() = 0;
  virtual void displayOff() = 0;
};
class Arduino_OLED : public Arduino_TFT { public: virtual void setBrightness(uint8_t brightness) = 0; };
class Arduino_CO5300 : public Arduino_OLED {
 public:
  Arduino_CO5300(Arduino_DataBus* bus, int8_t rst = GFX_NOT_DEFINED, uint8_t r = 0, int16_t w = 480,
                 int16_t h = 480, uint8_t col_offset1 = 0, uint8_t row_offset1 = 0,
                 uint8_t col_offset2 = 0, uint8_t row_offset2 = 0);
  bool begin(int32_t speed = GFX_NOT_DEFINED) override;
  void displayOn() override;
  void displayOff() override;
  void setBrightness(uint8_t brightness) override;
};
class Arduino_Canvas : public Arduino_GFX {
 public:
  Arduino_Canvas(int16_t w, int16_t h, Arduino_G* output, int16_t output_x = 0, int16_t output_y = 0,
                 uint8_t rotation = 0);
  bool begin(int32_t speed = GFX_NOT_DEFINED) override;
  void flush(bool force_flush = false);
};
