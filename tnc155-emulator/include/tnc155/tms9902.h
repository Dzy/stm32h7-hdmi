#ifndef TNC155_TMS9902_H
#define TNC155_TMS9902_H

#include <stdbool.h>
#include <stdint.h>

typedef struct tnc155_tms9902 {
    uint8_t control;
    uint8_t interval;
    uint16_t receive_rate;
    uint16_t transmit_rate;
    uint8_t receive_buffer;
    uint8_t transmit_buffer;
    bool ldctrl, ldir, lrdr, lxdr;
    bool rts, break_on;
    bool receiver_irq_enable, transmitter_irq_enable;
    bool timer_irq_enable, data_set_irq_enable;
    bool receive_loaded, transmit_empty, shift_empty;
    bool framing_error, overrun_error, parity_error;
    bool tx_pending;
    uint32_t reset_count, rx_count, tx_count;
} tnc155_tms9902;

void tnc155_tms9902_reset(tnc155_tms9902 *device);
bool tnc155_tms9902_read_cru(const tnc155_tms9902 *device, unsigned bit);
void tnc155_tms9902_write_cru(tnc155_tms9902 *device, unsigned bit,
                              bool value);
bool tnc155_tms9902_interrupt(const tnc155_tms9902 *device);
bool tnc155_tms9902_receive(tnc155_tms9902 *device, uint8_t value);
bool tnc155_tms9902_take_transmit(tnc155_tms9902 *device, uint8_t *value);

#endif
