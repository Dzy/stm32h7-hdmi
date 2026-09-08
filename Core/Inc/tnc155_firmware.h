#ifndef TNC155_FIRMWARE_H
#define TNC155_FIRMWARE_H

#include <stdbool.h>
#include <stdint.h>

bool TNC155_Firmware_Init(void);
void TNC155_Firmware_Task(void);
void TNC155_Firmware_SysTickISR(void);
void TNC155_Firmware_LTDCReloadComplete(void);

extern volatile uint32_t g_tnc155_last_slice_core_cycles;
extern volatile uint32_t g_tnc155_max_slice_core_cycles;
extern volatile uint32_t g_tnc155_last_frame_core_cycles;
extern volatile uint8_t g_tnc155_faulted;

#endif /* TNC155_FIRMWARE_H */
