/**
 * @file lv_port_indev_templ.c
 *
 */

 /*Copy this file as "lv_port_indev.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"

#include "usb_host.h"

#include "usbh_hid.h"
#include "usbh_hid_mouse.h"
#include "usbh_hid_parser.h"
/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void mouse_init(void);
static bool mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static bool mouse_is_pressed(void);
static void mouse_get_xy(lv_coord_t * x, lv_coord_t * y);

/**********************
 *  STATIC VARIABLES
 **********************/
lv_indev_t * indev_mouse;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_IMG_MOUSE_ARGB
#define LV_ATTRIBUTE_IMG_IMG_MOUSE_ARGB
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_IMG_IMG_MOUSE_ARGB uint8_t img_mouse_argb_map[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x84, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x84, 0x9f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x84, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x84, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x10, 0x84, 0x3f, 0x00, 0x00, 0x00, 0x10, 0x84, 0x9f, 0x00, 0x00, 0x00, 0x10, 0x84, 0xff, 0x00, 0x00, 0x00, 0x10, 0x84, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x10, 0x70, 0xeb, 0x5a, 0xff, 0x00, 0x00, 0x34, 0xae, 0x73, 0xff, 0x00, 0x00, 0x07, 0xf0, 0x83, 0xa0, 0x00, 0x00, 0x00, 0x10, 0x84, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x08, 0xa7, 0x00, 0x00, 0x74, 0x00, 0x00, 0x44, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x83, 0x00, 0x00, 0x4b, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xe8, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xd8, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xd0, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf0, 0xff, 0x00, 0xd0, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf0, 0xff, 0x00, 0xe8, 0xff, 0x00, 0xc8, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf0, 0xff, 0x00, 0xe8, 0xff, 0x00, 0xe8, 0xff, 0x00, 0xe8, 0xff, 0x00, 0xc0, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf0, 0xff, 0x00, 0xe8, 0xff, 0x00, 0xe8, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xc0, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xe8, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xd8, 0xff, 0x00, 0xb8, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xe8, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xd8, 0xff, 0x00, 0xd8, 0xff, 0x00, 0xd0, 0xff, 0x00, 0xb0, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf0, 0xff, 0x00, 0xe8, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xd8, 0xff, 0x00, 0xd0, 0xff, 0x00, 0xd0, 0xff, 0x00, 0xd0, 0xff, 0x00, 0xc8, 0xff, 0x00, 0xb0, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xd8, 0xff, 0x00, 0xd0, 0xff, 0x00, 0xd0, 0xff, 0x00, 0xc8, 0xff, 0x00, 0xc0, 0xff, 0x00, 0xc0, 0xff, 0x00, 0xc0, 0xff, 0x00, 0xb8, 0xff, 0x00, 0xa0, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xd0, 0xff, 0x00, 0xc0, 0xff, 0x00, 0xb8, 0xff, 0x00, 0xb0, 0xff, 0x00, 0xa8, 0xff, 0x00, 0xa0, 0xff, 0x00, 0xa0, 0xff, 0x00, 0xa0, 0xff, 0x00, 0xa0, 0xff, 0x00, 0x88, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x87, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x20, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xe8, 0xff, 0x00, 0xd8, 0xff, 0x00, 0xc0, 0xff, 0x00, 0xb0, 0xff, 0x00, 0xa0, 0xff, 0x00, 0x90, 0xff, 0x00, 0x88, 0xff, 0x00, 0x88, 0xff, 0x00, 0x88, 0xff, 0x00, 0x88, 0xff, 0x00, 0x88, 0xff, 0x00, 0x88, 0xff, 0x00, 0x70, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xcf, 0x00, 0x00, 0x83, 0x00, 0x00, 0x47, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x07, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xf0, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xc8, 0xff, 0x00, 0xa8, 0xff, 0x00, 0x78, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x60, 0x00, 0x00, 0x28, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf8, 0xff, 0x00, 0xe8, 0xff, 0x00, 0xd0, 0xff, 0x00, 0xb0, 0xff, 0x00, 0x78, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xfc, 0x00, 0x00, 0xf8, 0x00, 0x00, 0xf4, 0x00, 0x00, 0xf0, 0x00, 0x00, 0xef, 0x00, 0x00, 0xec, 0x00, 0x00, 0xec, 0x00, 0x00, 0xec, 0x00, 0x00, 0xeb, 0x00, 0x00, 0xe3, 0x00, 0x00, 0xcc, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x68, 0x00, 0x00, 0x2f, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf0, 0xff, 0x00, 0xd8, 0xff, 0x00, 0xb8, 0xff, 0x00, 0x78, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xfc, 0x00, 0x00, 0xf4, 0x00, 0x00, 0xe8, 0x00, 0x00, 0xd8, 0x00, 0x00, 0xcc, 0x00, 0x00, 0xc7, 0x00, 0x00, 0xc4, 0x00, 0x00, 0xc4, 0x00, 0x00, 0xc4, 0x00, 0x00, 0xc4, 0x00, 0x00, 0xbf, 0x00, 0x00, 0xab, 0x00, 0x00, 0x88, 0x00, 0x00, 0x5c, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xe0, 0xff, 0x00, 0xc8, 0xff, 0x00, 0x88, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xfc, 0x00, 0x00, 0xf3, 0x00, 0x00, 0xdf, 0x00, 0x00, 0xc0, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x88, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x7b, 0x00, 0x00, 0x78, 0x00, 0x00, 0x78, 0x00, 0x00, 0x77, 0x00, 0x00, 0x74, 0x00, 0x00, 0x68, 0x00, 0x00, 0x53, 0x00, 0x00, 0x38, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xd0, 0xff, 0x00, 0x98, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xfc, 0x00, 0x00, 0xf3, 0x00, 0x00, 0xdc, 0x00, 0x00, 0xb7, 0x00, 0x00, 0x88, 0x00, 0x00, 0x5f, 0x00, 0x00, 0x44, 0x00, 0x00, 0x38, 0x00, 0x00, 0x34, 0x00, 0x00, 0x33, 0x00, 0x00, 0x33, 0x00, 0x00, 0x33, 0x00, 0x00, 0x30, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x23, 0x00, 0x00, 0x18, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x07, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xa8, 0xff, 0x00, 0x00, 0xff, 0x00, 0x00, 0xfc, 0x00, 0x00, 0xf3, 0x00, 0x00, 0xdc, 0x00, 0x00, 0xb4, 0x00, 0x00, 0x83, 0x00, 0x00, 0x54, 0x00, 0x00, 0x33, 0x00, 0x00, 0x20, 0x00, 0x00, 0x17, 0x00, 0x00, 0x14, 0x00, 0x00, 0x13, 0x00, 0x00, 0x13, 0x00, 0x00, 0x13, 0x00, 0x00, 0x13, 0x00, 0x00, 0x10, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x07, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x08, 0xfc, 0x00, 0x00, 0xf3, 0x00, 0x00, 0xf0, 0x00, 0x00, 0xdb, 0x00, 0x00, 0xb3, 0x00, 0x00, 0x83, 0x00, 0x00, 0x50, 0x00, 0x00, 0x2f, 0x00, 0x00, 0x18, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x07, 0x00, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0xc7, 0x00, 0x00, 0xd4, 0x00, 0x00, 0xd3, 0x00, 0x00, 0xac, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x50, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x17, 0x00, 0x00, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x8b, 0x00, 0x00, 0xa4, 0x00, 0x00, 0x9c, 0x00, 0x00, 0x74, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x17, 0x00, 0x00, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x57, 0x00, 0x00, 0x68, 0x00, 0x00, 0x5f, 0x00, 0x00, 0x40, 0x00, 0x00, 0x28, 0x00, 0x00, 0x17, 0x00, 0x00, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x24, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x13, 0x00, 0x00, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
};

const lv_img_dsc_t img_mouse_argb = {
  .header.always_zero = 0,
  .header.w = 32,
  .header.h = 36,
  .data_size = 1152 * LV_IMG_PX_SIZE_ALPHA_BYTE,
  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
  .data = img_mouse_argb_map,
};
void lv_port_indev_init(void)
{
    /**
     * Here you will find example implementation of input devices supported by LittelvGL:
     *  - Touchpad
     *  - Mouse (with cursor support)
     *  - Keypad (supports GUI usage only with key)
     *  - Encoder (supports GUI usage only with: left, right, push)
     *  - Button (external buttons to press points on the screen)
     *
     *  The `..._read()` function are only examples.
     *  You should shape them according to your hardware
     */

    static lv_indev_drv_t indev_drv;

    /*------------------
     * Mouse
     * -----------------*/

    /*Initialize your touchpad if you have*/
    mouse_init();

    /*Register a mouse input device*/
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mouse_read;
    indev_mouse = lv_indev_drv_register(&indev_drv);

    LV_IMG_DECLARE(img_mouse_argb)

    /*Set cursor. For simplicity set a HOME symbol now.*/
    lv_obj_t * mouse_cursor = lv_img_create(lv_disp_get_scr_act(NULL), NULL);
    lv_img_set_src(mouse_cursor, &img_mouse_argb);
    lv_indev_set_cursor(indev_mouse, mouse_cursor);

}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*------------------
 * Mouse
 * -----------------*/

/*Initialize your mouse*/
static void mouse_init(void)
{
    /*Your code comes here*/
}

int32_t MouseX, MouseY;
uint8_t MouseB[3];

void USBH_HID_EventCallback(USBH_HandleTypeDef *phost)
{
  if(USBH_HID_GetDeviceType(phost) == HID_MOUSE)
  {
    HID_MOUSE_Info_TypeDef *Mouse_Info;
    Mouse_Info = USBH_HID_GetMouseInfo(phost);  // Get the info
    MouseX += (int8_t)Mouse_Info->x;  // get the x value
    MouseY += (int8_t)Mouse_Info->y;  // get the y value

    if(MouseX<0) MouseX = 0;
    if(MouseY<0) MouseY = 0;
    if(MouseX>1279) MouseX = 1279;
    if(MouseY>1023) MouseY = 1023;

    MouseB[0] = Mouse_Info->buttons[0];
    MouseB[1] = Mouse_Info->buttons[1];
    MouseB[2] = Mouse_Info->buttons[2];
  }
}

/*Will be called by the library to read the mouse*/
static bool mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    MX_USB_HOST_Process();

    /*Get the current x and y coordinates*/
    mouse_get_xy(&data->point.x, &data->point.y);

    /*Get whether the mouse button is pressed or released*/
    if(mouse_is_pressed()) {
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

    /*Return `false` because we are not buffering and no more data to read*/
    return false;
}

/*Return true is the mouse button is pressed*/
static bool mouse_is_pressed(void)
{
    /*Your code comes here*/
    if(MouseB[0]) {
        return true;
    }
    return false;
}

/*Get the x and y coordinates if the mouse is pressed*/
static void mouse_get_xy(lv_coord_t * x, lv_coord_t * y)
{
    /*Your code comes here*/

    (*x) = MouseX;
    (*y) = MouseY;
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
