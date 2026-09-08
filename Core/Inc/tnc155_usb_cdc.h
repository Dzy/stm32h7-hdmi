#ifndef TNC155_USB_CDC_H
#define TNC155_USB_CDC_H

#include "tnc155/i8279.h"
#include "tnc155/serial_keyboard.h"

#include <stdbool.h>

void TNC155_USB_CDC_Init(tnc155_serial_keyboard *keyboard,
                         tnc155_i8279 *i8279);
void TNC155_USB_CDC_Task(void);
void TNC155_USB_CDC_IRQHandler(void);
bool TNC155_USB_CDC_Connected(void);

#endif /* TNC155_USB_CDC_H */
