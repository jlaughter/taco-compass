#pragma once
#include <stdint.h>
typedef int esp_err_t;
esp_err_t esp_sleep_enable_gpio_wakeup(void);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us);
esp_err_t esp_light_sleep_start(void);
