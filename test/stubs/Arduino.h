// Minimal Arduino-ESP32 surface for host type-checking only (not linked).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <initializer_list>
#define ARDUINO 10819
#define HIGH 1
#define HEX 16
#define BIN 2
#define DEC 10
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09
#define SDA 8
#define SCL 9
typedef bool boolean;
typedef uint8_t byte;
uint32_t millis();
uint32_t esp_random();
uint32_t micros();
void delay(uint32_t);
void delayMicroseconds(uint32_t);
int digitalRead(uint8_t);
void digitalWrite(uint8_t, uint8_t);
void pinMode(uint8_t, uint8_t);
#define IRAM_ATTR
#define digitalPinToInterrupt(p) (p)
void attachInterrupt(uint8_t, void (*)(void), int);
void detachInterrupt(uint8_t);

class __FlashStringHelper;
class String { public: String(const char* = ""); const char* c_str() const; };
class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
  size_t print(const char*); size_t print(int, int = 10); size_t print(unsigned, int = 10); size_t print(long, int = 10); size_t print(unsigned long, int = 10); size_t print(double, int = 2);
  size_t println(const char* = ""); size_t println(int, int = 10); size_t println(unsigned, int = 10); size_t println(long, int = 10); size_t println(unsigned long, int = 10); size_t println(double, int = 2);
  size_t printf(const char*, ...) __attribute__((format(printf, 2, 3)));
};
class Stream : public Print {
 public:
  virtual int available() = 0; virtual int read() = 0; virtual int peek() = 0; virtual void flush() {}
  size_t readBytes(uint8_t* buffer, size_t length); size_t readBytes(char* buffer, size_t length);
};
class HWCDC : public Stream {
 public:
  void begin(unsigned long); size_t write(uint8_t) override; int available() override; int read() override; int peek() override;
  operator bool() const;
};
extern HWCDC Serial;
// FreeRTOS bits
typedef void* TaskHandle_t;
typedef uint32_t TickType_t_fwd;
typedef void* SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffUL
SemaphoreHandle_t xSemaphoreCreateMutex();
int xSemaphoreTake(SemaphoreHandle_t, uint32_t);
int xSemaphoreGive(SemaphoreHandle_t);
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef struct { int x; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
void vPortEnterCritical(portMUX_TYPE*); void vPortExitCritical(portMUX_TYPE*);
#define portENTER_CRITICAL(m) vPortEnterCritical(m)
#define portEXIT_CRITICAL(m) vPortExitCritical(m)
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
void vTaskDelay(TickType_t);
BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, unsigned, TaskHandle_t*, int);
void* ps_malloc(size_t);
#define log_e(...) do{}while(0)
#define log_d(...) do{}while(0)
#define log_i(...) do{}while(0)
#define log_w(...) do{}while(0)
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t*)(a))

// ---- Function-like and constant macros copied from arduino-esp32 3.3.10 cores/esp32/Arduino.h,
// so name collisions (e.g. a helper called degrees()) fail here like they do on the real core.
#define PI         3.1415926535897932384626433832795
#define HALF_PI    1.5707963267948966192313216916398
#define TWO_PI     6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define EULER      2.718281828459045235360287471352
#define SERIAL  0x0
#define DISPLAY 0x1
#define LSBFIRST 0
#define MSBFIRST 1
#define RISING    0x01
#define FALLING   0x02
#define CHANGE    0x03
#define ONLOW     0x04
#define ONHIGH    0x05
#define DEFAULT  1
#define EXTERNAL 0
#define _min(a, b)                ((a) < (b) ? (a) : (b))
#define _max(a, b)                ((a) > (b) ? (a) : (b))
#define _abs(x)                   ((x) > 0 ? (x) : -(x))
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define _round(x)                 ((x) >= 0 ? (long)((x) + 0.5) : (long)((x) - 0.5))
#define radians(deg)              ((deg) * DEG_TO_RAD)
#define degrees(rad)              ((rad) * RAD_TO_DEG)
#define sq(x)                     ((x) * (x))
#define portENABLE_INTERRUPTS()
#define portDISABLE_INTERRUPTS()
#define sei()          portENABLE_INTERRUPTS()
#define cli()          portDISABLE_INTERRUPTS()
#define interrupts()   sei()
#define noInterrupts() cli()
#define lowByte(w)  ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
#define bitRead(value, bit)            (((value) >> (bit)) & 0x01)
#define bitSet(value, bit)             ((value) |= (1UL << (bit)))
#define bitClear(value, bit)           ((value) &= ~(1UL << (bit)))
#define bitToggle(value, bit)          ((value) ^= (1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define bit(b) (1UL << (b))
#define _BV(b) (1UL << (b))
#define word(...) makeWord(__VA_ARGS__)

// HardwareSerial subset (arduino-esp32 3.3.10 cores/esp32/HardwareSerial.h)
#define SERIAL_8N1 0x800001c
class HardwareSerial : public Stream {
 public:
  void begin(unsigned long baud, uint32_t config = SERIAL_8N1, int8_t rxPin = -1, int8_t txPin = -1,
             bool invert = false, unsigned long timeout_ms = 20000UL, uint8_t rxfifo_full_thrhd = 120);
  void end(void);
  size_t setRxBufferSize(size_t new_size);
  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  int available() override;
  int read() override;
  int peek() override;
};
extern HardwareSerial Serial1;
