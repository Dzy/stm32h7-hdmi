/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */
/* LTDC timing/pixel-format entry selected from LTDCSYNC[]. */
#define LTDC_VID_FORMAT 10U

/* External SDRAM and framebuffer layout.
 *
 * Two 4 MiB slots are reserved for L8 framebuffers.  The first 8 MiB is
 * overlaid by a non-cacheable MPU region so CPU, DMA2D and LTDC always see
 * the same bytes without per-frame cache maintenance.  The remaining SDRAM
 * stays cacheable; the depth buffer starts immediately after the two slots.
 */
#define SDRAM_BASE_ADDRESS              0xC0000000U
#define SDRAM_SIZE_BYTES                (32U * 1024U * 1024U)
#define FRAMEBUFFER_SLOT_SIZE_BYTES     (4U * 1024U * 1024U)
#define FRAMEBUFFER0_ADDRESS            (SDRAM_BASE_ADDRESS)
#define FRAMEBUFFER1_ADDRESS            (SDRAM_BASE_ADDRESS + FRAMEBUFFER_SLOT_SIZE_BYTES)
#define FRAMEBUFFER_MPU_SIZE_BYTES      (8U * 1024U * 1024U)
/* Keep LVGL's working set off the external SDRAM bus.  The H743 has 512 KiB
 * AXI SRAM at 0x24000000 and this linker script deliberately does not place
 * normal .data/.bss there.  A 128 KiB reservation holds the 64-line RGB332
 * draw buffer; the following 256 KiB is the LVGL TLSF heap. */
#define AXI_SRAM_BASE_ADDRESS            0x24000000U
#define AXI_SRAM_SIZE_BYTES              (512U * 1024U)
#define LVGL_DRAW_BUFFER_ADDRESS         (AXI_SRAM_BASE_ADDRESS)
#define LVGL_DRAW_BUFFER_RESERVE_BYTES   (128U * 1024U)
#define LVGL_HEAP_ADDRESS                (AXI_SRAM_BASE_ADDRESS + LVGL_DRAW_BUFFER_RESERVE_BYTES)
#define LVGL_HEAP_SIZE_BYTES             (256U * 1024U)

/* Retained for compatibility with the original 3D project; LVGL does not use it. */
#define DEPTH_BUFFER_ADDRESS             (SDRAM_BASE_ADDRESS + FRAMEBUFFER_MPU_SIZE_BYTES)

/* If a fault happens after video has been initialized, paint the framebuffer
 * with a diagnostic RGB332 index instead of silently leaving a black screen. */
void Video_FatalPattern(uint8_t color_index);

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define USB_FAULT_Pin GPIO_PIN_2
#define USB_FAULT_GPIO_Port GPIOE
#define VBUS_EN_Pin GPIO_PIN_3
#define VBUS_EN_GPIO_Port GPIOE
/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
