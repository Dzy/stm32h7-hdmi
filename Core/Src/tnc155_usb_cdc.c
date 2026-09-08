#include "tnc155_usb_cdc.h"

#include "main.h"
#include "tusb.h"

#include <stddef.h>
#include <stdint.h>

#define TNC155_USB_RHPORT 0u
#define TNC155_USB_CDC_INDEX 0u

static tnc155_serial_keyboard *s_keyboard;
static tnc155_i8279 *s_i8279;
static volatile bool s_connected;

static void usb_fs_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    HAL_PWREx_EnableUSBVoltageDetector();

    /* Keep the exact FS pin mux already proven by the Cube USB-host build. */
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF10_OTG1_FS;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* VBUS sense is connected on PA9 on this board. */
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = 0u;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_NVIC_SetPriority(OTG_FS_IRQn, 5u, 0u);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

void TNC155_USB_CDC_Init(tnc155_serial_keyboard *keyboard,
                         tnc155_i8279 *i8279)
{
    tusb_rhport_init_t host_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_AUTO
    };

    s_keyboard = keyboard;
    s_i8279 = i8279;
    s_connected = false;

    if (s_keyboard != NULL)
        tnc155_serial_keyboard_init(s_keyboard);

    /* PE3 drives the external VBUS switch active-low. Keep VBUS off while the
       DWC2 host is configured, then apply power after TinyUSB owns the core. */
    HAL_GPIO_WritePin(VBUS_EN_GPIO_Port, VBUS_EN_Pin, GPIO_PIN_SET);
    usb_fs_gpio_init();

    if (!tusb_init(TNC155_USB_RHPORT, &host_init))
        Error_Handler();

    HAL_GPIO_WritePin(VBUS_EN_GPIO_Port, VBUS_EN_Pin, GPIO_PIN_RESET);
}

void TNC155_USB_CDC_Task(void)
{
    uint8_t buffer[128];

    tuh_task();

    if (s_keyboard == NULL || s_i8279 == NULL ||
        !tuh_cdc_mounted(TNC155_USB_CDC_INDEX))
        return;

    for (;;) {
        uint32_t count = tuh_cdc_read(TNC155_USB_CDC_INDEX,
                                      buffer, sizeof(buffer));
        if (count == 0u)
            break;
        tnc155_serial_keyboard_feed(s_keyboard, s_i8279, buffer, count);
    }
}

void TNC155_USB_CDC_IRQHandler(void)
{
    tusb_int_handler(TNC155_USB_RHPORT, true);
}

bool TNC155_USB_CDC_Connected(void)
{
    return s_connected;
}

uint32_t tusb_time_millis_api(void)
{
    return HAL_GetTick();
}

void tuh_cdc_mount_cb(uint8_t idx)
{
    if (idx == TNC155_USB_CDC_INDEX)
        s_connected = true;
}

void tuh_cdc_umount_cb(uint8_t idx)
{
    if (idx != TNC155_USB_CDC_INDEX)
        return;

    s_connected = false;
    if (s_keyboard != NULL) {
        s_keyboard->line_length = 0u;
        s_keyboard->line_overflow = false;
        s_keyboard->key_down = false;
        s_keyboard->active_kd_code = 0u;
    }
}
