/**
 * LVGL v8.4 configuration for STM32H743 + LTDC L8 at 1920x1080.
 *
 * The LTDC CLUT is programmed as RGB332, therefore LVGL's native 8-bit
 * lv_color_t byte is also the exact byte written into the L8 framebuffer.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Native framebuffer format: RGB332 -> LTDC L8 CLUT index. */
#define LV_COLOR_DEPTH                  8
#define LV_COLOR_16_SWAP                0
#define LV_COLOR_SCREEN_TRANSP          0

/* LVGL heap lives in internal AXI SRAM.  This keeps allocator and widget
 * traffic away from the SDRAM bus while LTDC is continuously scanning 1080p. */
#define LV_MEM_CUSTOM                   0
#define LV_MEM_SIZE                     (256U * 1024U)
#define LV_MEM_ADR                      0x24020000UL
#define LV_MEM_BUF_MAX_NUM              24
#define LV_MEMCPY_MEMSET_STD            1

/* 60 Hz display target; actual frame rate is naturally load-dependent. */
#define LV_DISP_DEF_REFR_PERIOD         16
#define LV_INDEV_DEF_READ_PERIOD        8

/* HAL_GetTick() is already the board's millisecond timebase. */
#define LV_TICK_CUSTOM                  1
#define LV_TICK_CUSTOM_INCLUDE          "stm32h7xx_hal.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR    (HAL_GetTick())

#define LV_DPI_DEF                      160

/* Keep the full software renderer: the Widgets demo uses gradients,
 * rounded corners, arcs, meters and shadows. */
#define LV_DRAW_COMPLEX                 1
#define LV_SHADOW_CACHE_SIZE            32
#define LV_CIRCLE_CACHE_SIZE            8
#define LV_LAYER_SIMPLE_BUF_SIZE        (64U * 1024U)
#define LV_LAYER_SIMPLE_FALLBACK_BUF_SIZE (8U * 1024U)
#define LV_GRADIENT_MAX_STOPS           2
#define LV_GRAD_CACHE_DEF_SIZE          (16U * 1024U)

/* Ordered gradient dithering makes RGB332 gradients much less banded without
 * the much higher cost of error-diffusion dithering. */
#define LV_DITHER_GRADIENT              1
#define LV_DITHER_ERROR_DIFFUSION       0

/* DMA2D cannot emit L8, so using the generic LVGL DMA2D backend would add a
 * conversion step instead of accelerating this native RGB332/L8 path. */
#define LV_USE_GPU_STM32_DMA2D          0
#define LV_USE_GPU_ARM2D                0

#define LV_USE_LOG                      0
#define LV_USE_ASSERT_NULL              1
#define LV_USE_ASSERT_MALLOC            1
#define LV_USE_ASSERT_STYLE             0
#define LV_USE_ASSERT_MEM_INTEGRITY     0
#define LV_USE_ASSERT_OBJ               0

/* Useful on-board instrumentation for this Full-HD performance test. */
#define LV_USE_PERF_MONITOR             1
#define LV_USE_PERF_MONITOR_POS         LV_ALIGN_BOTTOM_RIGHT
#define LV_USE_MEM_MONITOR              1
#define LV_USE_MEM_MONITOR_POS          LV_ALIGN_BOTTOM_LEFT
#define LV_USE_REFR_DEBUG               0

#define LV_SPRINTF_CUSTOM               0
#define LV_SPRINTF_USE_FLOAT            0
#define LV_USE_USER_DATA                1
#define LV_ENABLE_GC                    0
#define LV_BIG_ENDIAN_SYSTEM            0
#define LV_USE_LARGE_COORD              0

/* Fonts used by lv_demo_widgets() in the large-display layout. */
#define LV_FONT_MONTSERRAT_12           1
#define LV_FONT_MONTSERRAT_14           1
#define LV_FONT_MONTSERRAT_16           1
#define LV_FONT_MONTSERRAT_18           0
#define LV_FONT_MONTSERRAT_20           1
#define LV_FONT_MONTSERRAT_22           0
#define LV_FONT_MONTSERRAT_24           1
#define LV_FONT_DEFAULT                 &lv_font_montserrat_16

/* Layout/theme engines used heavily by the official complex widgets demo. */
#define LV_USE_FLEX                     1
#define LV_USE_GRID                     1
#define LV_USE_THEME_DEFAULT            1
#define LV_THEME_DEFAULT_DARK           1
#define LV_THEME_DEFAULT_GROW           1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

/* Official LVGL complex demonstration. */
#define LV_USE_DEMO_WIDGETS             0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER  0
#define LV_USE_DEMO_BENCHMARK           0
#define LV_USE_DEMO_STRESS              0
#define LV_USE_DEMO_MUSIC               0

#endif /* LV_CONF_H */
