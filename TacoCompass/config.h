// TacoCompass — user-tunable settings
// Board: Waveshare ESP32-S3-Touch-AMOLED-1.75-G (SKU 31264, LC76G GNSS)
#pragma once

// ---------- Onboard bus (fixed by the board) ----------
#define PIN_I2C_SDA        15
#define PIN_I2C_SCL        14
#define PIN_LCD_SDIO0       4
#define PIN_LCD_SDIO1       5
#define PIN_LCD_SDIO2       6
#define PIN_LCD_SDIO3       7
#define PIN_LCD_SCLK       38
#define PIN_LCD_CS         12
#define PIN_LCD_RST        39
#define PIN_TP_RST         40
#define PIN_TP_INT         11
#define PIN_BOOT_BTN        0     // BOOT button, low when pressed
#define LCD_W             466
#define LCD_H             466

// The LC76G and everything else share I2C0. 100 kHz is what Waveshare's
// LC76G example uses; try 400000 once everything works (faster GPS reads).
#define I2C0_HZ        100000

// ---------- GPS link ----------
// The LC76G can be read over I2C (stock board) or UART. UART is far more robust: on this
// board the I2C side can jam until a long power-off. UART needs two pads bridged on the
// board: R15 (GPS TXD -> GPIO18) and R16 (GPS RXD <- GPIO17), both "NC" from the factory.
#define GPS_USE_UART             1      // 1 = R15 (and optionally R16) bridged; 0 = I2C
#define GPS_UART_RX             18      // ESP32 RX  <- LC76G TXD (via R15)
#define GPS_UART_TX             17      // ESP32 TX  -> LC76G RXD (via R16)
#define GPS_UART_BAUD       115200      // LC76G default
#define GPS_TRIM_AT_RESET        0      // 1 = send GGA+RMC-only config after every GPS reset

// ---------- External magnetometer (second I2C bus, header H2) ----------
// Header pin 4 = GPIO44 (U0RXD), pin 5 = GPIO43 (U0TXD), 3V3 = pin 3, GND = pin 2.
// UART0 is free because the console runs over native USB CDC.
#define PIN_MAG_SDA        43      // header pin 4 (silkscreen TXD)
#define PIN_MAG_SCL        44      // header pin 5 (silkscreen RXD)
#define MAG_I2C_HZ     400000

// Mapping from the magnetometer chip's axes to the BOARD frame.
// Board frame: +X = toward 3 o'clock on the screen, +Y = toward 12 o'clock,
//              +Z = out of the screen toward your face.
// Each entry picks a sensor axis (0=x,1=y,2=z) and a sign.
// Use the serial "raw" command (see README) to set these after mounting.
// Case build (2026-11-19): the module sits face-down behind the board, flipped left-to-right,
// so its X and Z point the other way (east/west showed mirrored). Check with "raw" lying
// flat, screen up: mag Z must be negative in the northern hemisphere.
#define MAG_MAP_X_AXIS 0
#define MAG_MAP_X_SIGN -1
#define MAG_MAP_Y_AXIS 1
#define MAG_MAP_Y_SIGN +1
#define MAG_MAP_Z_AXIS 2
#define MAG_MAP_Z_SIGN -1

// Same thing for the onboard QMI8658 accelerometer. Measured on the bench with "raw":
//   flat, screen up      -> chip z = -1.07   (chip Z points out the BACK)
//   3 o'clock edge raised -> chip y = +0.42  (chip +Y = board +X)
//   12 o'clock edge raised -> chip x = +0.51 (chip +X = board +Y)
#define ACC_MAP_X_AXIS 1
#define ACC_MAP_X_SIGN +1
#define ACC_MAP_Y_AXIS 0
#define ACC_MAP_Y_SIGN +1
#define ACC_MAP_Z_AXIS 2
#define ACC_MAP_Z_SIGN -1

// ---------- Touch ----------
// Left/right = next/previous target (Finder), up/down = next/previous page.
// Mounted upside down (e.g. the GPS antenna cable only reaches if the board is turned)?
// Set this to 1: it turns the picture, the touch, the accelerometer and the magnetometer
// 180 degrees together, so everything stays consistent. Afterwards redo the magnetometer
// calibration ("uncal", then calibrate) and re-zero the level.
#define DISPLAY_FLIP_180         0

// Left/right backwards -> TOUCH_INVERT_X; up/down backwards -> TOUCH_INVERT_Y;
// left/right acts like up/down (axes swapped) -> TOUCH_SWAP_XY.
#define TOUCH_SWAP_XY            0
#define TOUCH_INVERT_X           1      // reversed on the bench, 2026-11-13
#define TOUCH_INVERT_Y           1      // reversed on the bench, 2026-11-13
#define SWIPE_MIN_PX            70

// ---------- Behaviour ----------
#define NUM_NEAREST             5      // how many Taco Bells the scroll button cycles
#define ARRIVED_METERS        40.0f    // "you're here" radius
#define GPS_HEADING_MIN_MPS    2.5f    // above ~5.6 mph trust GPS course, not the magnetometer
#define GPS_HEADING_HYST_MPS   0.8f
#define HEADING_SMOOTH_ALPHA  0.18f    // 0..1, higher = snappier needle
#define BRIGHTNESS             200     // 0..255 AMOLED brightness
#define CAL_SECONDS             20     // magnetometer calibration duration
#define LONG_PRESS_MS         2500     // BOOT held this long -> calibration
#define HOME_SET_HOLD_MS      1500     // PWR + BOOT held together this long -> Home = here
#define NAP_AFTER_STILL_S      180     // auto-nap after this long with no motion (0 = never)
#define POWEROFF_AFTER_NAP_S  1800     // hard power-off after napping this long (0 = never)
#define SAVE_FIX_EVERY_S        60     // persist last good fix to flash this often
#define DEFAULT_METRIC           0     // fresh-device units: 0 = Freedom Units, 1 = metric
                                       // (change any time over serial: units freedom|metric)

// ---------------------------------------------------------------- About page
// The last page (swipe to it) and the power-on screen. Personalize it in about.h,
// next to this file; see about.example.h. Inline icons: {heart} {tree} {taco} {star}.
#if __has_include("about.h")
#include "about.h"
#endif
#ifndef ABOUT_TITLE
#define ABOUT_TITLE  ""        // "" = the device name from the database
#endif
#ifndef ABOUT_LINE1
#define ABOUT_LINE1  ""
#endif
#ifndef ABOUT_LINE2
#define ABOUT_LINE2  ""
#endif
#ifndef ABOUT_LINE3
#define ABOUT_LINE3  ""
#endif
#ifndef ABOUT_SCROLL
#define ABOUT_SCROLL ""        // "" = no scrolling line
#endif
#ifndef ABOUT_BOOT_MS
#define ABOUT_BOOT_MS 3500     // how long it shows at power-on (0 = skip)
#endif
