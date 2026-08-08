/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32H743 Full-HD LVGL v8.4 RGB332/L8 demo
  ******************************************************************************
  */

#include "main.h"
#include "gpio.h"
#include "rtc.h"
#include "fmc.h"
#include "dma2d.h"
#include "i2c.h"
#include "i2s.h"
#include "dma.h"
#include "adc.h"
#include "ltdc.h"
#include "usb_host.h"
#include "tda998x.h"
#include "lv_port.h"
#include "pixel_lab.h"

#include "usbh_hid.h"
#include "usbh_hid_mouse.h"

#include "lvgl.h"

#include <stdint.h>
#include <string.h>

void SystemClock_Config(void);
void MX_USB_HOST_Process(void);
static void MPU_Conf(void);
static void Framebuffer_Clear(uint32_t address);
static volatile uint8_t video_ready = 0U;

void USBH_HID_EventCallback(USBH_HandleTypeDef *phost)
{
    if(USBH_HID_GetDeviceType(phost) != HID_MOUSE) {
        return;
    }

    HID_MOUSE_Info_TypeDef *mouse = USBH_HID_GetMouseInfo(phost);
    if(mouse == NULL) {
        return;
    }

    /* HID x/y are signed 8-bit relative deltas even though ST stores them in
     * uint8_t fields. Button 0 is the normal LVGL primary pointer button. */
    LVGL_MouseFeed((int8_t)mouse->x, (int8_t)mouse->y,
                   (mouse->buttons[0] != 0U) ? 1U : 0U);
}

static void Framebuffer_Clear(uint32_t address)
{
    const size_t bytes = (size_t)LTDCSYNC[LTDC_VID_FORMAT].ahw *
                         (size_t)LTDCSYNC[LTDC_VID_FORMAT].avh;

    if(bytes > FRAMEBUFFER_SLOT_SIZE_BYTES) {
        Error_Handler();
    }

    /* Region 1 maps both framebuffer slots as Normal non-cacheable. */
    memset((void *)(uintptr_t)address, 0, bytes);
    __DSB();
}

void Video_FatalPattern(uint8_t color_index)
{
    if(video_ready == 0U) {
        return;
    }

    const uint32_t packed = (uint32_t)color_index |
                            ((uint32_t)color_index << 8) |
                            ((uint32_t)color_index << 16) |
                            ((uint32_t)color_index << 24);
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)FRAMEBUFFER0_ADDRESS;
    const uint32_t words = ((uint32_t)LTDCSYNC[LTDC_VID_FORMAT].ahw *
                            (uint32_t)LTDCSYNC[LTDC_VID_FORMAT].avh) / 4U;

    for(uint32_t i = 0U; i < words; ++i) {
        fb[i] = packed;
    }
    __DSB();
}

static void MPU_Conf(void)
{
    MPU_Region_InitTypeDef region = {0};

    HAL_MPU_Disable();

    /* Keep the same known-good SDRAM MPU policy as the rasterizer build.
     * The first 8 MiB will be overlaid non-cacheable for the LTDC buffers. */
    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER0;
    region.BaseAddress = SDRAM_BASE_ADDRESS;
    region.Size = MPU_REGION_SIZE_32MB;
    region.SubRegionDisable = 0x00U;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);

    /* Higher-priority overlay for both 4 MiB L8 framebuffer slots. LTDC sees
     * CPU writes immediately; no per-flush D-cache clean operation is needed. */
    region.Number = MPU_REGION_NUMBER1;
    region.BaseAddress = FRAMEBUFFER0_ADDRESS;
    region.Size = MPU_REGION_SIZE_8MB;
    region.TypeExtField = MPU_TEX_LEVEL1;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);

    /* Explicitly mark the complete 512 KiB AXI SRAM as normal write-back
     * cacheable memory. LVGL's draw buffer and TLSF heap are CPU-only here. */
    region.Number = MPU_REGION_NUMBER2;
    region.BaseAddress = AXI_SRAM_BASE_ADDRESS;
    region.Size = MPU_REGION_SIZE_512KB;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

int main(void)
{
    MPU_Conf();

    SCB_EnableICache();
    SCB_EnableDCache();

    HAL_Init();
    SystemClock_Config();

    /* FMC/LTDC pins run at high edge rates; enable the H7 compensation cell. */
    __HAL_RCC_CSI_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_EnableCompensationCell();

    /* Keep the board bring-up order as close as possible to the known-good
     * rasterizer build. Only the application layer changes to LVGL. */
    MX_GPIO_Init();
    MX_RTC_Init();
    MX_FMC_Init();
    MX_DMA2D_Init();

    Framebuffer_Clear(FRAMEBUFFER0_ADDRESS);
    Framebuffer_Clear(FRAMEBUFFER1_ADDRESS);

    MX_LTDC_Init();
    LVGL_LTDCPrepare();
    video_ready = 1U;

    MX_I2C1_Init();
    MX_I2S1_Init();
    MX_DMA_Init();
    MX_USB_HOST_Init();
    MX_ADC3_Init();
    HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);

    /* Program the existing TDA998x HDMI transmitter only after LTDC already
     * produces a stable, visible RGB332 test pattern. This matches the proven
     * rasterizer initialization sequence. */
    tda_init();

    /* Leave the startup bars visible briefly. If a later LVGL fault occurs,
     * the fault handlers overwrite them with a diagnostic solid color. */
    HAL_Delay(250U);

    lv_init();
    LVGL_PortInit();

    /* Native 1920x1080 diagnostic dashboard plus direct 1-pixel tests. */
    PixelLab_Create();

    /* Force the first refresh to become due before entering the superloop. */
    HAL_Delay(20U);
    (void)lv_timer_handler();

    uint32_t next_lvgl_ms = HAL_GetTick();

    while(1) {
        /* Keep USB Host HID serviced independently of LVGL render load. */
        MX_USB_HOST_Process();

        const uint32_t now = HAL_GetTick();
        if(!PixelTest_IsActive() && (int32_t)(now - next_lvgl_ms) >= 0) {
            uint32_t delay_ms = lv_timer_handler();

            /* Never defer LVGL for long: this keeps pointer movement smooth
             * while still giving USB host processing the majority of tight-loop
             * iterations between render/timer passes. */
            if(delay_ms < 1U) delay_ms = 1U;
            if(delay_ms > 5U) delay_ms = 5U;
            next_lvgl_ms = now + delay_ms;
        }

        /* Direct pixel-test patterns are drawn after any LVGL work from this
         * iteration, so a click-triggered LVGL refresh cannot overwrite them. */
        PixelTest_Service();
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    __HAL_RCC_PLL_PLLSOURCE_CONFIG(RCC_PLLSOURCE_HSE);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 120;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 20;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                  RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC |
                                               RCC_PERIPHCLK_LTDC |
                                               RCC_PERIPHCLK_SPI1 |
                                               RCC_PERIPHCLK_ADC |
                                               RCC_PERIPHCLK_I2C1 |
                                               RCC_PERIPHCLK_USB |
                                               RCC_PERIPHCLK_FMC;

    /* FMC kernel 216 MHz -> SDRAM clock 108 MHz. */
    PeriphClkInitStruct.PLL2.PLL2M = 1;
    PeriphClkInitStruct.PLL2.PLL2N = 54;
    PeriphClkInitStruct.PLL2.PLL2P = 6;
    PeriphClkInitStruct.PLL2.PLL2Q = 6;
    PeriphClkInitStruct.PLL2.PLL2R = 2;
    PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_3;
    PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    PeriphClkInitStruct.PLL2.PLL2FRACN = 0;

    /* LTDC mode 8: CTA VIC 16, 1920x1080p60 at 148.5 MHz. */
    PeriphClkInitStruct.PLL3.PLL3M = 8;
    PeriphClkInitStruct.PLL3.PLL3N = LTDCSYNC[LTDC_VID_FORMAT].pll3n;
    PeriphClkInitStruct.PLL3.PLL3P = LTDCSYNC[LTDC_VID_FORMAT].pll3p;
    PeriphClkInitStruct.PLL3.PLL3Q = LTDCSYNC[LTDC_VID_FORMAT].pll3q;
    PeriphClkInitStruct.PLL3.PLL3R = LTDCSYNC[LTDC_VID_FORMAT].pll3r;
    PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0;
    PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    PeriphClkInitStruct.PLL3.PLL3FRACN = 0;

    PeriphClkInitStruct.FmcClockSelection = RCC_FMCCLKSOURCE_PLL2;
    PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;
    PeriphClkInitStruct.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
    PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL2;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;

    if(HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
        Error_Handler();
    }

    HAL_PWREx_EnableUSBVoltageDetector();
}

void Error_Handler(void)
{
    __disable_irq();
    /* Solid red (RGB332 0xE0) means an explicit HAL/application error. */
    Video_FatalPattern(0xE0U);
    while(1) {
        __NOP();
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    Error_Handler();
}
#endif
