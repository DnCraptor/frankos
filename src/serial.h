/*
 * FRANK OS — PIO UART Serial Driver (ESP-01 Netcard)
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://rh1.tech
 *
 * PIO-based UART on PIO1 for communicating with the ESP-01 netcard.
 * Must be initialized AFTER snd_init() (audio resets PIO1).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>
#include <stdint.h>

/* Initialize PIO UART on PIO1 (claims 2 state machines for TX and RX).
 * Uses GPIO 20 (RX from ESP TX) and GPIO 21 (TX to ESP RX) at 115200 baud.
 * MUST be called after snd_init() because audio does a full PIO1 reset. */
void serial_init(void);

/* Send a single character */
void serial_send_char(char c);

/* Send a null-terminated string */
void serial_send_string(const char *s);

/* Send raw binary data */
void serial_send_data(const uint8_t *data, uint16_t len);

/* Returns true if at least one byte is available in the RX ring buffer */
bool serial_readable(void);

/* Read one byte from the RX ring buffer (blocks if empty) */
uint8_t serial_read_byte(void);

#endif /* SERIAL_H */
