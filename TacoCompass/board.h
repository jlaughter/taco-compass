// Everything on the onboard I2C bus (AXP2101, TCA9554, QMI8658, CST9217 touch, LC76G),
// serviced by one FreeRTOS task on core 0 so slow GPS reads never stall the UI.
#pragma once
#include <Arduino.h>
#include "heading.h"
#include "nmea.h"

struct BoardSnapshot {
  GpsFix fix;
  Vec3 accel = {0, 0, 1};    // g, BOARD frame (axis map applied)
  bool accelOk = false;
  int battPct = -1;
  bool charging = false;
  bool usb = false;
  bool pwrDown = false;          // live PWR button level (TCA9554 EXIO4)
  uint32_t pwrShortPresses = 0;  // monotonic counter of AXP PWR short presses
  uint32_t swipesLeft = 0;       // monotonic counters of recognized swipes
  uint32_t swipesRight = 0;
  uint32_t swipesUp = 0;
  uint32_t swipesDown = 0;
  uint32_t taps = 0;             // short, still touches
  uint32_t lastTouchMs = 0;
  uint32_t gpsBytes = 0;
  // GPS link diagnostics: polls attempted, I2C failures, last length the LC76G reported,
  // stuck transactions cleared, module resets, ms it took to answer after the last reset
  // (+1; 0 = didn't answer), and '$PAIR001,062,0' config acks seen.
  uint32_t gpsPolls = 0, gpsI2cErrs = 0, gpsLastLen = 0, gpsUnsticks = 0;
  uint32_t gpsResets = 0, gpsReadyMs = 0, gpsCfgAcks = 0;
};

namespace board {
bool begin();                   // call once from setup(), before the display
void snapshot(BoardSnapshot& out);
void setNapping(bool napping);  // pauses GPS/IMU/touch polling
void pollPowerKey();            // service the PWR key directly (used while napping)
void powerOff();                // AXP2101 hard off; PWR button turns it back on
bool imuOk();
bool pmuOk();
bool touchOk();
void i2cScan(Print& out);        // list every address that ACKs on the onboard bus
void gpsProbe(Print& out);       // run one GPS poll with its steps printed
void gpsUartListen(Print& out);  // listen for NMEA on GPIO18/GPIO17 at common bauds
void gpsReset();                 // ask the sensor task to reset the LC76G (logs to Serial)
void gpsTrim();                  // ask it to send the GGA+RMC-only sentence config
}
