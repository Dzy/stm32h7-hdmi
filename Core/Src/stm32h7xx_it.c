#include "main.h"
#include "stm32h7xx_it.h"

#ifdef TNC155_FIRMWARE
#include "tnc155_usb_cdc.h"
#else
extern HCD_HandleTypeDef hhcd_USB_OTG_FS;
#endif

extern LTDC_HandleTypeDef hltdc;

void NMI_Handler(void)
{
    while (1) {}
}

void HardFault_Handler(void)
{
    while (1) {}
}

void MemManage_Handler(void)
{
    while (1) {}
}

void BusFault_Handler(void)
{
    while (1) {}
}

void UsageFault_Handler(void)
{
    while (1) {}
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void LTDC_IRQHandler(void)
{
    HAL_LTDC_IRQHandler(&hltdc);
}

void LTDC_ER_IRQHandler(void)
{
    HAL_LTDC_IRQHandler(&hltdc);
}

void OTG_FS_IRQHandler(void)
{
#ifdef TNC155_FIRMWARE
    TNC155_USB_CDC_IRQHandler();
#else
    HAL_HCD_IRQHandler(&hhcd_USB_OTG_FS);
#endif
}
