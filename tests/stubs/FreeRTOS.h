#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
#define configTICK_RATE_HZ 1000u
#define taskENTER_CRITICAL() do {} while (0)
#define taskEXIT_CRITICAL() do {} while (0)
#endif
