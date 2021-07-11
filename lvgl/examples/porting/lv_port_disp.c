/**
 * @file lv_port_disp_templ.c
 *
 */

 /*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include "ltdc.h"
/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
#if LV_USE_GPU
static void gpu_blend(lv_disp_drv_t * disp_drv, lv_color_t * dest, const lv_color_t * src, uint32_t length, lv_opa_t opa);
static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
        const lv_area_t * fill_area, lv_color_t color);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

static lv_disp_draw_buf_t draw_buf_dsc;
static lv_color_t *buf_1 = 0xc0000000+(1024*1024*0);
static lv_color_t *buf_2 = 0xc0000000+(1024*1024*4);

void lv_port_disp_init(void) {

    disp_init();
    lv_disp_draw_buf_init(&draw_buf_dsc, buf_2, buf_1, LV_HOR_RES_MAX * LV_VER_RES_MAX);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LV_HOR_RES_MAX;
    disp_drv.ver_res = LV_VER_RES_MAX;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf_dsc;

#if LV_USE_GPU
    disp_drv.gpu_fill_cb = gpu_fill;
#endif

    lv_disp_drv_register(&disp_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /*You code here*/
}

extern uint32_t lv_flush_screen;
extern uint32_t ReloadFlag;

/*Flush the content of the internal buffer the specific area on the display
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_disp_flush_ready()' has to be called when finished.*/
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    /*The most simple case (but also the slowest) to put all pixels to the screen one-by-one*/

    //HAL_LTDC_SetAddress_NoReload(&hltdc, color_p, 0);
    //ReloadFlag = 0;
    //HAL_LTDC_Reload(&hltdc,LTDC_RELOAD_VERTICAL_BLANKING);

    //lv_disp_flush_ready(disp_drv);
    HAL_LTDC_SetAddress_NoReload(&hltdc, color_p, 0);
    HAL_LTDC_Reload(&hltdc,LTDC_RELOAD_VERTICAL_BLANKING);
    lv_flush_screen = 1;
}

/*OPTIONAL: GPU INTERFACE*/
#if LV_USE_GPU

/*If your MCU has hardware accelerator (GPU) then you can use it to fill a memory with a color*/
static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
                    const lv_area_t * fill_area, lv_color_t color)
{
    /*It's an example code which should be done by your GPU*/
    int32_t x, y;
    dest_buf += dest_width * fill_area->y1; /*Go to the first line*/

    for(y = fill_area->y1; y <= fill_area->y2; y++) {
        for(x = fill_area->x1; x <= fill_area->x2; x++) {
            dest_buf[x] = color;
        }
        dest_buf+=dest_width;    /*Go to the next line*/
    }
}

#endif  /*LV_USE_GPU*/

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
