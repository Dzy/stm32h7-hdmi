#ifndef LV_PORT_H
#define LV_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void LVGL_LTDCPrepare(void);
void LVGL_PortInit(void);
void LVGL_MouseFeed(int8_t dx, int8_t dy, uint8_t left_pressed);

enum {
    PIXEL_TEST_CHECKER = 0,
    PIXEL_TEST_VSTRIPES,
    PIXEL_TEST_HSTRIPES,
    PIXEL_TEST_CROSSHAIR,
    PIXEL_TEST_RGB_RAMP,
    PIXEL_TEST_COUNT
};

void PixelTest_Start(uint8_t mode);
uint8_t PixelTest_IsActive(void);
void PixelTest_Service(void);

#ifdef __cplusplus
}
#endif

#endif
