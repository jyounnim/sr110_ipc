/*
 * Lab 01: Hello IPC — shared message type (used by both HOST/M55 and CLIENT/M4)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LAB01_IPC_COMMON_H
#define LAB01_IPC_COMMON_H

#include <stdint.h>

/* M55 -> M4 ping message. */
struct ipc_ping_msg {
	uint32_t seq;
};

/*
 * M4 -> M55 diagnostic ack (added 2026-08-30).
 *
 * This lab was originally meant to be M55->M4 one-way only (bidirectional
 * comes in Lab 03), but while the M4 physical console wasn't available yet,
 * this ack was added so the M55 console alone could confirm "is M4 actually
 * receiving pings / did the LED write succeed?". Kept as a permanent
 * diagnostic/regression-test aid even now that the M4 console works.
 */
struct ipc_ack_msg {
	uint32_t seq;
	int32_t led_ret; /* gpio_pin_toggle_dt() return value: 0 = success,
			   * negative = error code,
			   * -1000 = led0 device not ready (recorded as-is from main()) */
};

#endif /* LAB01_IPC_COMMON_H */
