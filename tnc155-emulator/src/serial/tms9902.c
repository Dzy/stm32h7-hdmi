#include "tnc155/tms9902.h"

#include <string.h>

void tnc155_tms9902_reset(tnc155_tms9902 *d)
{
    uint32_t resets = d != NULL ? d->reset_count + 1u : 0u;
    if (d == NULL) return;
    memset(d, 0, sizeof(*d));
    d->reset_count = resets;
    d->ldctrl = d->ldir = d->lrdr = d->lxdr = true;
    d->transmit_empty = d->shift_empty = true;
}

bool tnc155_tms9902_interrupt(const tnc155_tms9902 *d)
{
    return d != NULL &&
        ((d->receive_loaded && d->receiver_irq_enable) ||
         (d->transmit_empty && d->transmitter_irq_enable));
}

bool tnc155_tms9902_read_cru(const tnc155_tms9902 *d, unsigned bit)
{
    if (d == NULL || bit > 31u) return false;
    if (bit < 8u) return ((d->receive_buffer >> bit) & 1u) != 0u;
    switch (bit) {
    case 9: return d->framing_error || d->overrun_error || d->parity_error;
    case 10: return d->parity_error;
    case 11: return d->overrun_error;
    case 12: return d->framing_error;
    case 16: return d->receive_loaded && d->receiver_irq_enable;
    case 17: return d->transmit_empty && d->transmitter_irq_enable;
    case 21: return d->receive_loaded;
    case 22: return d->transmit_empty;
    case 23: return d->shift_empty;
    case 26: return d->rts;
    case 30: return d->ldctrl || d->ldir || d->lrdr || d->lxdr || d->break_on;
    case 31: return tnc155_tms9902_interrupt(d);
    default: return false;
    }
}

void tnc155_tms9902_write_cru(tnc155_tms9902 *d, unsigned bit, bool value)
{
    uint16_t mask;
    if (d == NULL || bit > 31u) return;
    if (bit == 31u) { tnc155_tms9902_reset(d); return; }
    if (bit < 12u && (d->ldctrl || d->ldir || d->lrdr || d->lxdr)) {
        mask = (uint16_t)(1u << bit);
        if (d->ldctrl && bit < 8u) {
            d->control = (uint8_t)((d->control & ~mask) | (value ? mask : 0u));
            if (bit == 7u) d->ldctrl = false;
        }
        if (d->ldir && bit < 8u) {
            d->interval = (uint8_t)((d->interval & ~mask) | (value ? mask : 0u));
            if (bit == 7u) d->ldir = false;
        }
        if (d->lrdr && bit < 11u) {
            d->receive_rate = (uint16_t)((d->receive_rate & ~mask) |
                                         (value ? mask : 0u));
            if (bit == 10u) d->lrdr = false;
        }
        if (d->lxdr && bit < 12u) {
            d->transmit_rate = (uint16_t)((d->transmit_rate & ~mask) |
                                          (value ? mask : 0u));
            if (bit == 11u) d->lxdr = false;
        }
        return;
    }
    if (bit < 8u) {
        mask = (uint16_t)(1u << bit);
        d->transmit_buffer = (uint8_t)((d->transmit_buffer & ~mask) |
                                       (value ? mask : 0u));
        if (bit == 7u) {
            d->transmit_empty = false;
            d->tx_pending = true;
            ++d->tx_count;
        }
        return;
    }
    switch (bit) {
    case 11: d->lxdr = value; break;
    case 12: d->lrdr = value; break;
    case 13: d->ldir = value; break;
    case 14: d->ldctrl = value; break;
    case 16: d->rts = value; break;
    case 17: d->break_on = value; break;
    case 18: d->receiver_irq_enable = value; d->receive_loaded = false; break;
    case 19: d->transmitter_irq_enable = value; break;
    case 20: d->timer_irq_enable = value; break;
    case 21: d->data_set_irq_enable = value; break;
    default: break;
    }
}

bool tnc155_tms9902_receive(tnc155_tms9902 *d, uint8_t value)
{
    if (d == NULL) return false;
    if (d->receive_loaded) d->overrun_error = true;
    d->receive_buffer = value;
    d->receive_loaded = true;
    ++d->rx_count;
    return true;
}

bool tnc155_tms9902_take_transmit(tnc155_tms9902 *d, uint8_t *value)
{
    if (d == NULL || value == NULL || !d->tx_pending) return false;
    *value = d->transmit_buffer;
    d->tx_pending = false;
    d->transmit_empty = d->shift_empty = true;
    return true;
}
