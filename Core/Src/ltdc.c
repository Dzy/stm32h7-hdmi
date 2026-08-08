/**
  ******************************************************************************
  * @file    ltdc.c
  * @brief   This file provides code for the configuration
  *          of the LTDC instances.
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

/* Includes ------------------------------------------------------------------*/
#include "ltdc.h"

/* USER CODE BEGIN 0 */
const LTDCSYNC_t LTDCSYNC[] = {
 /* PLL3                         Active       Horizontal timing       Vertical timing         Sync polarity                         HDMI AVI */
 /* N    P  Q  R                W     H       FP   SW   BP             FP  SW  BP              H       V                           VIC Aspect */
 { 252,  2, 2, 8,              640,  480,    24,  40, 128,             9,  3, 28,  LTDC_HSPOLARITY_AL, LTDC_VSPOLARITY_AL,   0, HDMI_AVI_ASPECT_NONE }, /*  0: VESA DMT 640x480 @ 72 Hz */
 { 252,  2, 2, 8,              640,  480,    16,  64, 120,             1,  3, 16,  LTDC_HSPOLARITY_AL, LTDC_VSPOLARITY_AL,   0, HDMI_AVI_ASPECT_NONE }, /*  1: VESA DMT 640x480 @ 75 Hz */
 { 400,  2, 2, 8,              800,  600,    56, 120,  64,            37,  6, 23,  LTDC_HSPOLARITY_AH, LTDC_VSPOLARITY_AH,   0, HDMI_AVI_ASPECT_NONE }, /*  2: VESA DMT 800x600 @ 72 Hz */
 { 396,  2, 2, 8,              800,  600,    16,  80, 160,             1,  3, 21,  LTDC_HSPOLARITY_AH, LTDC_VSPOLARITY_AH,   0, HDMI_AVI_ASPECT_NONE }, /*  3: VESA DMT 800x600 @ 75 Hz */
 { 450,  2, 2, 8,              800,  600,    32,  64, 152,             1,  3, 27,  LTDC_HSPOLARITY_AH, LTDC_VSPOLARITY_AH,   0, HDMI_AVI_ASPECT_NONE }, /*  4: VESA DMT 800x600 @ 85 Hz */
 { 300,  4, 4, 4,             1024,  768,    24, 136, 144,             3,  6, 29,  LTDC_HSPOLARITY_AL, LTDC_VSPOLARITY_AL,   0, HDMI_AVI_ASPECT_NONE }, /*  5: VESA DMT 1024x768 @ 70 Hz */
 { 315,  4, 4, 4,             1024,  768,    16,  96, 176,             1,  3, 28,  LTDC_HSPOLARITY_AH, LTDC_VSPOLARITY_AH,   0, HDMI_AVI_ASPECT_NONE }, /*  6: VESA DMT 1024x768 @ 75 Hz */
 { 432,  4, 4, 4,             1280, 1024,    48, 112, 248,             1,  3, 38,  LTDC_HSPOLARITY_AH, LTDC_VSPOLARITY_AH,   0, HDMI_AVI_ASPECT_NONE }, /*  7: VESA DMT 1280x1024 @ 60 Hz */
 { 297,  4, 4, 2,             1920, 1080,    88,  44, 148,             4,  5, 36,  LTDC_HSPOLARITY_AH, LTDC_VSPOLARITY_AH,  16, HDMI_AVI_ASPECT_16_9 }, /*  8: CTA-861 VIC 16, 1920x1080p60 -- DEFAULT */
 { 297,  4, 4, 4,             1280,  720,   110,  40, 220,             5,  5, 20,  LTDC_HSPOLARITY_AH, LTDC_VSPOLARITY_AH,   4, HDMI_AVI_ASPECT_16_9 }, /*  9: CTA-861 VIC 4, 1280x720p60 */
 { 297,  4, 4, 4,             1280,  720,   440,  40, 220,             5,  5, 20,  LTDC_HSPOLARITY_AH, LTDC_VSPOLARITY_AH,  19, HDMI_AVI_ASPECT_16_9 }, /* 10: CTA-861 VIC 19, 1280x720p50 */
 { 297,  4, 4, 2,             1920, 1080,   528,  44, 148,             4,  5, 36,  LTDC_HSPOLARITY_AH, LTDC_VSPOLARITY_AH,  31, HDMI_AVI_ASPECT_16_9 }, /* 11: CTA-861 VIC 31, 1920x1080p50 */
 { 297,  4, 4, 4,             1920, 1080,    88,  44, 148,             4,  5, 36,  LTDC_HSPOLARITY_AH, LTDC_VSPOLARITY_AH,  34, HDMI_AVI_ASPECT_16_9 }, /* 12: CTA-861 VIC 34, 1920x1080p30 -- RATE DIAGNOSTIC */
};

#define LTDC_MODE_COUNT (sizeof(LTDCSYNC) / sizeof(LTDCSYNC[0]))
_Static_assert(LTDC_VID_FORMAT < LTDC_MODE_COUNT, "LTDC_VID_FORMAT is outside LTDCSYNC[]");    

/* USER CODE END 0 */

LTDC_HandleTypeDef hltdc;

/* LTDC init function */
void MX_LTDC_Init(void)
{
  LTDC_LayerCfgTypeDef pLayerCfg;

  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDCSYNC[LTDC_VID_FORMAT].hpol;
  hltdc.Init.VSPolarity = LTDCSYNC[LTDC_VID_FORMAT].vpol;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AH;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IIPC;

  hltdc.Init.HorizontalSync     = (LTDCSYNC[LTDC_VID_FORMAT].hsw - 1);
  hltdc.Init.VerticalSync       = (LTDCSYNC[LTDC_VID_FORMAT].vsh - 1);
  hltdc.Init.AccumulatedHBP     = (LTDCSYNC[LTDC_VID_FORMAT].hsw + LTDCSYNC[LTDC_VID_FORMAT].hbp - 1);
  hltdc.Init.AccumulatedVBP     = (LTDCSYNC[LTDC_VID_FORMAT].vsh + LTDCSYNC[LTDC_VID_FORMAT].vbp - 1);
  hltdc.Init.AccumulatedActiveW = (LTDCSYNC[LTDC_VID_FORMAT].hsw + LTDCSYNC[LTDC_VID_FORMAT].ahw + LTDCSYNC[LTDC_VID_FORMAT].hbp  - 1);
  hltdc.Init.AccumulatedActiveH = (LTDCSYNC[LTDC_VID_FORMAT].vsh + LTDCSYNC[LTDC_VID_FORMAT].avh + LTDCSYNC[LTDC_VID_FORMAT].vbp  - 1);
  hltdc.Init.TotalWidth         = (LTDCSYNC[LTDC_VID_FORMAT].hsw + LTDCSYNC[LTDC_VID_FORMAT].ahw + LTDCSYNC[LTDC_VID_FORMAT].hbp + LTDCSYNC[LTDC_VID_FORMAT].hfp - 1);
  hltdc.Init.TotalHeigh         = (LTDCSYNC[LTDC_VID_FORMAT].vsh + LTDCSYNC[LTDC_VID_FORMAT].avh + LTDCSYNC[LTDC_VID_FORMAT].vbp + LTDCSYNC[LTDC_VID_FORMAT].vfp - 1);

  hltdc.Init.Backcolor.Blue = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red = 0;
  HAL_LTDC_Init(&hltdc);

  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = LTDCSYNC[LTDC_VID_FORMAT].ahw;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = LTDCSYNC[LTDC_VID_FORMAT].avh;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_L8; //RGB565;
  pLayerCfg.Alpha = 255;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  pLayerCfg.FBStartAdress = FRAMEBUFFER0_ADDRESS;
  pLayerCfg.ImageWidth = LTDCSYNC[LTDC_VID_FORMAT].ahw;
  pLayerCfg.ImageHeight = LTDCSYNC[LTDC_VID_FORMAT].avh;
  pLayerCfg.Backcolor.Blue = 255;
  pLayerCfg.Backcolor.Green = 255;
  pLayerCfg.Backcolor.Red = 255;
  HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0);

}

void HAL_LTDC_MspInit(LTDC_HandleTypeDef* ltdcHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct;
  if(ltdcHandle->Instance==LTDC)
  {
  /* USER CODE BEGIN LTDC_MspInit 0 */

  /* USER CODE END LTDC_MspInit 0 */
    /* LTDC clock enable */
    __HAL_RCC_LTDC_CLK_ENABLE();

    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    /**LTDC GPIO Configuration
    
    PF10    ------> LTDC_DE
    PA4     ------> LTDC_VSYNC
    PG7     ------> LTDC_CLK
    PC6     ------> LTDC_HSYNC
    
    PB0     ------> LTDC_R3
    PA5     ------> LTDC_R4
    PC0     ------> LTDC_R5
    PB1     ------> LTDC_R6
    PG6     ------> LTDC_R7

    PA6     ------> LTDC_G2
    PC9     ------> LTDC_G3
    PB10    ------> LTDC_G4
    PH4     ------> LTDC_G5
    PI11    ------> LTDC_G6
    PD3     ------> LTDC_G7

    PA8     ------> LTDC_B3
    PG12    ------> LTDC_B4
    PA3     ------> LTDC_B5
    PB8     ------> LTDC_B6
    PB9     ------> LTDC_B7
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_LTDC;
    HAL_GPIO_Init(GPIOI, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_4;
    //GPIO_InitStruct.Alternate = GPIO_AF9_LTDC;
    HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
    //GPIO_InitStruct.Alternate = GPIO_AF9_LTDC;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_12;
    //GPIO_InitStruct.Alternate = GPIO_AF9_LTDC;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_6;
    //GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6;
    //GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_8|GPIO_PIN_9;
    //GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6;
    //GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_7;
    //GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    //GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Alternate = GPIO_AF10_LTDC;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Alternate = GPIO_AF13_LTDC;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* LTDC interrupt Init */
    HAL_NVIC_SetPriority(LTDC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(LTDC_IRQn);
    HAL_NVIC_SetPriority(LTDC_ER_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(LTDC_ER_IRQn);
  /* USER CODE BEGIN LTDC_MspInit 1 */

  /* USER CODE END LTDC_MspInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
