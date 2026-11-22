#pragma once
typedef int esp_err_t;
typedef enum { GPIO_NUM_0 = 0 } gpio_num_t;
typedef enum { GPIO_INTR_LOW_LEVEL = 4, GPIO_INTR_HIGH_LEVEL = 5 } gpio_int_type_t;
esp_err_t gpio_wakeup_enable(gpio_num_t gpio_num, gpio_int_type_t intr_type);
