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
#include "tda998x.h"
#include "tnc155_firmware.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void SystemClock_Config(void);
static void MPU_Conf(void);
static void Framebuffer_Clear(uint32_t address);
static volatile uint8_t video_ready;

static void Framebuffer_Clear(uint32_t address)
{
    const size_t bytes = (size_t)LTDCSYNC[LTDC_VID_FORMAT].ahw *
                         (size_t)LTDCSYNC[LTDC_VID_FORMAT].avh;
    if (bytes > FRAMEBUFFER_SLOT_SIZE_BYTES)
        Error_Handler();
    memset((void *)(uintptr_t)address, 0, bytes);
    __DSB();
}

void Video_FatalPattern(uint8_t color_index)
{
    uint32_t packed;
    uint32_t words;
    volatile uint32_t *fb0;
    volatile uint32_t *fb1;
    uint32_t i;

    if (video_ready == 0u)
        return;

    packed = (uint32_t)color_index |
             ((uint32_t)color_index << 8) |
             ((uint32_t)color_index << 16) |
             ((uint32_t)color_index << 24);
    words = ((uint32_t)LTDCSYNC[LTDC_VID_FORMAT].ahw *
             (uint32_t)LTDCSYNC[LTDC_VID_FORMAT].avh) / 4u;
    fb0 = (volatile uint32_t *)(uintptr_t)FRAMEBUFFER0_ADDRESS;
    fb1 = (volatile uint32_t *)(uintptr_t)FRAMEBUFFER1_ADDRESS;

    for (i = 0u; i < words; ++i) {
        fb0[i] = packed;
        fb1[i] = packed;
    }
    __DSB();
}

static void MPU_Conf(void)
{
    MPU_Region_InitTypeDef region = {0};

    HAL_MPU_Disable();

    /* Cacheable general SDRAM. TNC machine state and the ARGB staging image
       live above the first 8 MiB and benefit from M7 D-cache. */
    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER0;
    region.BaseAddress = SDRAM_BASE_ADDRESS;
    region.Size = MPU_REGION_SIZE_32MB;
    region.SubRegionDisable = 0x00u;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);

    /* Both LTDC L8 framebuffer slots are non-cacheable so no clean operation
       is needed before a vertical-blanking page flip. */
    region.Number = MPU_REGION_NUMBER1;
    region.BaseAddress = FRAMEBUFFER0_ADDRESS;
    region.Size = MPU_REGION_SIZE_8MB;
    region.TypeExtField = MPU_TEX_LEVEL1;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
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

    __HAL_RCC_CSI_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_EnableCompensationCell();

    MX_GPIO_Init();
    MX_RTC_Init();
    MX_FMC_Init();
    MX_DMA2D_Init();

    Framebuffer_Clear(FRAMEBUFFER0_ADDRESS);
    Framebuffer_Clear(FRAMEBUFFER1_ADDRESS);

    MX_LTDC_Init();
    video_ready = 1u;

    MX_I2C1_Init();
    MX_I2S1_Init();
    MX_DMA_Init();
    MX_ADC3_Init();
    HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);

    /* HDMI transmitter comes up after LTDC is already generating stable
       1080p60 timing, matching the proven board initialization sequence. */
    tda_init();

    if (!TNC155_Firmware_Init())
        Error_Handler();

    for (;;) {
        TNC155_Firmware_Task();
        if (g_tnc155_faulted != 0u)
            Video_FatalPattern(0xe3u); /* magenta: emulator/runtime fault */
    }
}

void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *ltdc)
{
    if (ltdc != NULL && ltdc->Instance == LTDC)
        TNC155_Firmware_LTDCReloadComplete();
}

void HAL_LTDC_ErrorCallback(LTDC_HandleTypeDef *ltdc)
{
    uint32_t errors;

    if (ltdc == NULL || ltdc->Instance != LTDC)
        return;

    errors = ltdc->ErrorCode;
    if ((errors & HAL_LTDC_ERROR_FU) != 0u)
        ++g_tnc155_ltdc_fifo_underruns;
    if ((errors & HAL_LTDC_ERROR_TE) != 0u)
        ++g_tnc155_ltdc_transfer_errors;

    ltdc->ErrorCode &= ~(HAL_LTDC_ERROR_FU | HAL_LTDC_ERROR_TE);
    ltdc->State = HAL_LTDC_STATE_READY;
    __HAL_LTDC_ENABLE_IT(ltdc, LTDC_IT_FU | LTDC_IT_TE);
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
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

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
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
        Error_Handler();

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC |
                                               RCC_PERIPHCLK_LTDC |
                                               RCC_PERIPHCLK_SPI1 |
                                               RCC_PERIPHCLK_ADC |
                                               RCC_PERIPHCLK_I2C1 |
                                               RCC_PERIPHCLK_USB |
                                               RCC_PERIPHCLK_FMC;

    PeriphClkInitStruct.PLL2.PLL2M = 1;
    PeriphClkInitStruct.PLL2.PLL2N = 54;
    PeriphClkInitStruct.PLL2.PLL2P = 6;
    PeriphClkInitStruct.PLL2.PLL2Q = 6;
    PeriphClkInitStruct.PLL2.PLL2R = 2;
    PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_3;
    PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    PeriphClkInitStruct.PLL2.PLL2FRACN = 0;

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

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
        Error_Handler();

    HAL_PWREx_EnableUSBVoltageDetector();
}

void Error_Handler(void)
{
    __disable_irq();
    Video_FatalPattern(0xe0u); /* red: HAL/application initialization fault */
    while (1) {
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
