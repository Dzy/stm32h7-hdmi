#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_STM32H7
#endif

#define CFG_TUSB_OS                    OPT_OS_NONE
#define CFG_TUSB_DEBUG                 0
#define CFG_TUH_ENABLED                1
#define CFG_TUD_ENABLED                0
#define CFG_TUH_MAX_SPEED              OPT_MODE_FULL_SPEED
#define CFG_TUH_ENUMERATION_BUFSIZE    256
#define CFG_TUH_TASK_QUEUE_SZ          32
#define CFG_TUH_DEVICE_MAX             1
#define CFG_TUH_HUB                    0
#define CFG_TUH_CDC                    1
#define CFG_TUH_HID                    0
#define CFG_TUH_MSC                    0
#define CFG_TUH_VENDOR                 0

#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN              __attribute__((aligned(4)))

#define CFG_TUH_CDC_LINE_CONTROL_ON_ENUM \
    (CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS)
#define CFG_TUH_CDC_LINE_CODING_ON_ENUM \
    { 115200, CDC_LINE_CODING_STOP_BITS_1, CDC_LINE_CODING_PARITY_NONE, 8 }

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
