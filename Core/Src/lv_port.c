#include "lv_port.h"

#include "main.h"
#include "ltdc.h"
#include "lvgl.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LVGL_HOR_RES                 1920U
#define LVGL_VER_RES                 1080U
#define LVGL_DRAW_BUFFER_LINES       64U
#define LVGL_MOUSE_GAIN              1

_Static_assert(LTDC_VID_FORMAT == 8U,
               "This LVGL port is intentionally fixed to the 1920x1080 LTDC mode");
_Static_assert(sizeof(lv_color_t) == 1U,
               "LV_COLOR_DEPTH must be 8 so lv_color_t maps directly to LTDC L8");
_Static_assert(LV_MEM_ADR == LVGL_HEAP_ADDRESS,
               "lv_conf.h heap address and board AXI SRAM map disagree");
_Static_assert(LV_MEM_SIZE == LVGL_HEAP_SIZE_BYTES,
               "lv_conf.h heap size and board AXI SRAM map disagree");
_Static_assert((LVGL_HOR_RES * LVGL_DRAW_BUFFER_LINES * sizeof(lv_color_t)) <=
               LVGL_DRAW_BUFFER_RESERVE_BYTES,
               "LVGL draw buffer does not fit its AXI SRAM reservation");
_Static_assert((LVGL_DRAW_BUFFER_RESERVE_BYTES + LVGL_HEAP_SIZE_BYTES) <= AXI_SRAM_SIZE_BYTES,
               "LVGL AXI SRAM reservations exceed the H743 AXI SRAM");

static lv_disp_draw_buf_t draw_buf_dsc;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_indev_t *mouse_indev;

static volatile int16_t mouse_x = (int16_t)(LVGL_HOR_RES / 2U);
static volatile int16_t mouse_y = (int16_t)(LVGL_VER_RES / 2U);
static volatile uint8_t mouse_left;
static volatile uint8_t pixel_test_active;
static volatile uint8_t pixel_test_mode;
static volatile uint8_t pixel_test_clicks;
static volatile uint8_t pixel_test_last_left;
static volatile uint8_t pixel_test_redraw;

static uint8_t expand_3_to_8(uint32_t value)
{
    /* Exact endpoint mapping 0..7 -> 0..255. */
    return (uint8_t)((value * 255U + 3U) / 7U);
}

static uint8_t expand_2_to_8(uint32_t value)
{
    /* Exact endpoint mapping 0..3 -> 0..255. */
    return (uint8_t)((value * 255U + 1U) / 3U);
}

static void load_rgb332_clut(void)
{
    uint32_t clut[256];

    for(uint32_t i = 0U; i < 256U; ++i) {
        const uint8_t r = expand_3_to_8((i >> 5) & 0x07U);
        const uint8_t g = expand_3_to_8((i >> 2) & 0x07U);
        const uint8_t b = expand_2_to_8(i & 0x03U);
        clut[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }

    if(HAL_LTDC_ConfigCLUT(&hltdc, clut, 256U, 0U) != HAL_OK) {
        Error_Handler();
    }
    if(HAL_LTDC_EnableCLUT(&hltdc, 0U) != HAL_OK) {
        Error_Handler();
    }

    HAL_LTDC_DisableDither(&hltdc);
    __DSB();
}

void LVGL_LTDCPrepare(void)
{
    load_rgb332_clut();

    /* Eight bright RGB332 bars provide a deterministic power-on diagnostic.
     * If LVGL later faults, these remain visible instead of an all-black frame. */
    static const uint8_t bars[8] = {
        0xE0U, /* red */
        0x1CU, /* green */
        0x03U, /* blue */
        0xFCU, /* yellow */
        0xE3U, /* magenta */
        0x1FU, /* cyan */
        0xFFU, /* white */
        0x49U  /* gray */
    };

    uint8_t *fb = (uint8_t *)(uintptr_t)FRAMEBUFFER0_ADDRESS;
    const uint32_t bar_w = LVGL_HOR_RES / 8U;

    for(uint32_t y = 0U; y < LVGL_VER_RES; ++y) {
        for(uint32_t bar = 0U; bar < 8U; ++bar) {
            const uint32_t x0 = bar * bar_w;
            const uint32_t x1 = (bar == 7U) ? LVGL_HOR_RES : (bar + 1U) * bar_w;
            memset(fb + (size_t)y * LVGL_HOR_RES + x0, bars[bar], x1 - x0);
        }
    }

    __DSB();
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    (void)drv;

    /* LVGL clips invalidated areas to the display, so the common path is a
     * simple row copy. Keep the guard to prevent an accidental out-of-bounds
     * write if a future driver change violates that contract. */
    if((area->x2 < 0) || (area->y2 < 0) ||
       (area->x1 >= (lv_coord_t)LVGL_HOR_RES) ||
       (area->y1 >= (lv_coord_t)LVGL_VER_RES)) {
        lv_disp_flush_ready(drv);
        return;
    }

    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    const int32_t src_stride = (int32_t)lv_area_get_width(area);
    const uint8_t *src = (const uint8_t *)(const void *)color_p;

    if(x1 < 0) {
        src += (size_t)(-x1);
        x1 = 0;
    }
    if(y1 < 0) {
        src += (size_t)(-y1) * (size_t)src_stride;
        y1 = 0;
    }
    if(x2 >= (int32_t)LVGL_HOR_RES) x2 = (int32_t)LVGL_HOR_RES - 1;
    if(y2 >= (int32_t)LVGL_VER_RES) y2 = (int32_t)LVGL_VER_RES - 1;

    const size_t copy_width = (size_t)(x2 - x1 + 1);
    uint8_t *dst = (uint8_t *)(uintptr_t)FRAMEBUFFER0_ADDRESS +
                   (size_t)y1 * LVGL_HOR_RES + (size_t)x1;

    for(int32_t y = y1; y <= y2; ++y) {
        memcpy(dst, src, copy_width);
        src += src_stride;
        dst += LVGL_HOR_RES;
    }

    /* FRAMEBUFFER0 is Normal non-cacheable in MPU region 1. Ensure writes are
     * globally visible before telling LVGL it may reuse the draw buffer. */
    __DSB();
    lv_disp_flush_ready(drv);
}

static void mouse_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    /* Each field is a naturally aligned scalar. The callback runs from the
     * same bare-metal main context as USBH processing, so volatile snapshots
     * are sufficient and avoid disabling interrupts. */
    data->point.x = (lv_coord_t)mouse_x;
    data->point.y = (lv_coord_t)mouse_y;
    data->state = mouse_left ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

static void create_mouse_cursor(void)
{
    lv_obj_t *cursor = lv_obj_create(lv_scr_act());
    lv_obj_set_size(cursor, 26, 26);
    lv_obj_set_style_radius(cursor, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cursor, lv_color_hex(0x00D7FF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cursor, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_border_color(cursor, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(cursor, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cursor, 0, LV_PART_MAIN);
    lv_obj_clear_flag(cursor, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_indev_set_cursor(mouse_indev, cursor);
}


static inline void fb_put(uint32_t x, uint32_t y, uint8_t c)
{
    ((volatile uint8_t *)(uintptr_t)FRAMEBUFFER0_ADDRESS)[(size_t)y * LVGL_HOR_RES + x] = c;
}

static void pixel_test_draw(uint8_t mode)
{
    volatile uint8_t *fb = (volatile uint8_t *)(uintptr_t)FRAMEBUFFER0_ADDRESS;

    if(mode == PIXEL_TEST_CHECKER) {
        for(uint32_t y = 0; y < LVGL_VER_RES; ++y) {
            for(uint32_t x = 0; x < LVGL_HOR_RES; ++x)
                fb[(size_t)y * LVGL_HOR_RES + x] = ((x ^ y) & 1U) ? 0xFFU : 0x00U;
        }
    }
    else if(mode == PIXEL_TEST_VSTRIPES) {
        for(uint32_t y = 0; y < LVGL_VER_RES; ++y) {
            volatile uint8_t *row = fb + (size_t)y * LVGL_HOR_RES;
            for(uint32_t x = 0; x < LVGL_HOR_RES; ++x)
                row[x] = (x & 1U) ? 0xFFU : 0x00U;
        }
    }
    else if(mode == PIXEL_TEST_HSTRIPES) {
        for(uint32_t y = 0; y < LVGL_VER_RES; ++y)
            memset((void *)(fb + (size_t)y * LVGL_HOR_RES), (y & 1U) ? 0xFFU : 0x00U, LVGL_HOR_RES);
    }
    else if(mode == PIXEL_TEST_CROSSHAIR) {
        memset((void *)fb, 0x00, (size_t)LVGL_HOR_RES * LVGL_VER_RES);

        /* Exact single-pixel outer frame, center axes and 16-pixel ruler grid. */
        for(uint32_t x = 0; x < LVGL_HOR_RES; ++x) {
            fb_put(x, 0, 0xFFU);
            fb_put(x, LVGL_VER_RES - 1U, 0xFFU);
            fb_put(x, LVGL_VER_RES / 2U, 0x1FU);
        }
        for(uint32_t y = 0; y < LVGL_VER_RES; ++y) {
            fb_put(0, y, 0xFFU);
            fb_put(LVGL_HOR_RES - 1U, y, 0xFFU);
            fb_put(LVGL_HOR_RES / 2U, y, 0xE3U);
        }
        for(uint32_t x = 16U; x < LVGL_HOR_RES; x += 16U)
            for(uint32_t y = 4U; y < 12U; ++y) fb_put(x, y, 0xFCU);
        for(uint32_t y = 16U; y < LVGL_VER_RES; y += 16U)
            for(uint32_t x = 4U; x < 12U; ++x) fb_put(x, y, 0xFCU);

        /* Four 64x64 native checker patches near the corners. */
        const uint32_t ox[4] = {32U, LVGL_HOR_RES-96U, 32U, LVGL_HOR_RES-96U};
        const uint32_t oy[4] = {32U, 32U, LVGL_VER_RES-96U, LVGL_VER_RES-96U};
        for(uint32_t p = 0; p < 4U; ++p)
            for(uint32_t yy = 0; yy < 64U; ++yy)
                for(uint32_t xx = 0; xx < 64U; ++xx)
                    fb_put(ox[p]+xx, oy[p]+yy, ((xx ^ yy) & 1U) ? 0xFFU : 0x00U);
    }
    else {
        /* Every palette index appears in order across the width. The bottom
           64 lines are an exact 1-pixel RGB332 index sawtooth (0..255 repeat). */
        for(uint32_t y = 0; y < LVGL_VER_RES - 64U; ++y) {
            volatile uint8_t *row = fb + (size_t)y * LVGL_HOR_RES;
            for(uint32_t x = 0; x < LVGL_HOR_RES; ++x)
                row[x] = (uint8_t)((x * 256U) / LVGL_HOR_RES);
        }
        for(uint32_t y = LVGL_VER_RES - 64U; y < LVGL_VER_RES; ++y) {
            volatile uint8_t *row = fb + (size_t)y * LVGL_HOR_RES;
            for(uint32_t x = 0; x < LVGL_HOR_RES; ++x)
                row[x] = (uint8_t)(x & 0xFFU);
        }
    }
    __DSB();
}

void PixelTest_Start(uint8_t mode)
{
    if(mode >= PIXEL_TEST_COUNT) mode = PIXEL_TEST_CHECKER;
    pixel_test_mode = mode;
    pixel_test_clicks = 0U;
    pixel_test_last_left = 1U; /* Button is still down from the LVGL click. */
    pixel_test_active = 1U;
    pixel_test_redraw = 1U;
}

uint8_t PixelTest_IsActive(void)
{
    return pixel_test_active;
}

void PixelTest_Service(void)
{
    if(pixel_test_active && pixel_test_redraw) {
        pixel_test_redraw = 0U;
        pixel_test_draw(pixel_test_mode);
    }
}

static void pixel_test_button_update(uint8_t left_pressed)
{
    if(!pixel_test_active) return;

    if(left_pressed && !pixel_test_last_left) {
        ++pixel_test_clicks;
        if(pixel_test_clicks >= 5U) {
            pixel_test_active = 0U;
            lv_obj_invalidate(lv_scr_act());
        }
        else {
            pixel_test_mode = (uint8_t)((pixel_test_mode + 1U) % PIXEL_TEST_COUNT);
            pixel_test_redraw = 1U;
        }
    }
    pixel_test_last_left = left_pressed ? 1U : 0U;
}

void LVGL_MouseFeed(int8_t dx, int8_t dy, uint8_t left_pressed)
{
    int32_t x = (int32_t)mouse_x + (int32_t)dx * LVGL_MOUSE_GAIN;
    int32_t y = (int32_t)mouse_y + (int32_t)dy * LVGL_MOUSE_GAIN;

    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(x >= (int32_t)LVGL_HOR_RES) x = (int32_t)LVGL_HOR_RES - 1;
    if(y >= (int32_t)LVGL_VER_RES) y = (int32_t)LVGL_VER_RES - 1;

    mouse_x = (int16_t)x;
    mouse_y = (int16_t)y;
    mouse_left = (left_pressed != 0U) ? 1U : 0U;
    pixel_test_button_update(left_pressed);
}

void LVGL_PortInit(void)
{
    if((LTDCSYNC[LTDC_VID_FORMAT].ahw != LVGL_HOR_RES) ||
       (LTDCSYNC[LTDC_VID_FORMAT].avh != LVGL_VER_RES)) {
        Error_Handler();
    }

    lv_color_t *draw_buf = (lv_color_t *)(uintptr_t)LVGL_DRAW_BUFFER_ADDRESS;
    lv_disp_draw_buf_init(&draw_buf_dsc, draw_buf, NULL,
                          LVGL_HOR_RES * LVGL_DRAW_BUFFER_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = (lv_coord_t)LVGL_HOR_RES;
    disp_drv.ver_res = (lv_coord_t)LVGL_VER_RES;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf_dsc;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mouse_read_cb;
    mouse_indev = lv_indev_drv_register(&indev_drv);

    create_mouse_cursor();
}
