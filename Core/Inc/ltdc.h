/**
  ******************************************************************************
  * @file    ltdc.h
  * @brief   This file contains all the function prototypes for
  *          the ltdc.c file
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __LTDC_H__
#define __LTDC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern LTDC_HandleTypeDef hltdc;

typedef struct _LTDCSYNC_t {
   uint16_t pll3n, pll3p, pll3q, pll3r;
   uint16_t ahw, avh;
   uint16_t hfp, hsync, hbp;
   uint16_t vfp, vsync, vbp;
   uint16_t hsw,  ahbp, aaw, totalw;
   uint16_t vsh,  avbp, aah, totalh;
} LTDCSYNC_t;

static const LTDCSYNC_t LTDCSYNC[] = {
 { 252, 2, 2, 8, 640, 480, 24, 40, 128, 9, 3, 28, 39, 167, 807, 831, 2, 30, 510, 519 },         // 0 640x480_72Hz
 { 252, 2, 2, 8, 640, 480, 16, 96, 48, 11, 2, 32, 95, 143, 783, 799, 1, 33, 513, 524 },         // 1 640x480_75Hz
 { 288, 2, 2, 8, 640, 480, 32, 48, 112, 1, 3, 25, 47, 159, 799, 831, 2, 27, 507, 508 },         // 2 640x480_85Hz
 { 320, 2, 2, 8, 800, 600, 40, 128, 88, 1, 4, 23, 127, 215, 1015, 1055, 3, 26, 626, 627 },      // 3 800x600_60Hz
 { 400, 2, 2, 8, 800, 600, 56, 120, 64, 37, 6, 23, 119, 183, 983, 1039, 5, 28, 628, 665 },      // 4 800x600_72Hz
 { 396, 2, 2, 8, 800, 600, 16, 80, 160, 1, 2, 21, 79, 239, 1039, 1055, 1, 22, 622, 623 },       // 5 800x600_75Hz
 { 450, 2, 2, 8, 800, 600, 32, 64, 152, 1, 3, 27, 63, 215, 1015, 1047, 2, 29, 629, 630 },       // 6 800x600_85Hz
 { 265, 4, 4, 4, 1024, 768, 24, 136, 160, 3, 6, 29, 135, 295, 1319, 1343, 5, 34, 802, 805 },    // 7 1024x768_60Hz
 { 300, 4, 4, 4, 1024, 768, 24, 136, 144, 3, 6, 29, 135, 279, 1303, 1327, 5, 34, 802, 805 },    // 8 1024x768_70Hz
 { 315, 4, 4, 4, 1024, 768, 16, 96, 176, 1, 3, 28, 95, 271, 1295, 1311, 2, 30, 798, 799 },      // 9 1024x768_75Hz
 { 432, 4, 4, 4, 1280, 1024, 48, 112, 248, 1, 3, 38, 111, 359, 1639, 1687, 2, 40, 1064, 1065 }, //10 1280x1024_60Hz
};

/* USER CODE END Private defines */

void MX_LTDC_Init(void);

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __LTDC_H__ */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
