/*
 * Lab 01: Hello IPC — M55 -> M4 Ping (CLIENT, built for M4)
 *
 * Toggles LED0 (gpio_exp0, an I2C GPIO expander) whenever a ping arrives
 * from M55 over mbox.
 *
 * Important: the mbox rx callback runs in ISR context. LED0 sits behind an
 * I2C-based GPIO expander, so calling gpio_pin_toggle_dt() directly from
 * the ISR is not allowed (I2C transfers block). So the callback only
 * enqueues the message, and the actual toggle happens in a separate worker
 * thread (toggle_task). This ISR -> k_msgq -> worker thread pattern is
 * reused as the standard pattern in later labs too.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab01_client, CONFIG_LOG_DEFAULT_LEVEL);

#define TOGGLE_TASK_STACK_SIZE 1024
#define TOGGLE_TASK_PRIORITY   5

/* Already defined in the board dts as aliases { led0 = &led0; } (sits on top of gpio_exp0) */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

K_THREAD_STACK_DEFINE(toggle_task_stack, TOGGLE_TASK_STACK_SIZE);
static struct k_thread toggle_task_data;

/* Queue handing work off from the ISR callback to the worker thread (I2C access only from thread context) */
K_MSGQ_DEFINE(rx_msgq, sizeof(struct ipc_ping_msg), 4, 4);

static uint32_t g_msgq_drop_count;
static struct mbox_dt_spec tx_channel;
static bool led0_ready; /* 2026-08-30: flag so the IPC callback path stays alive even if led0 fails to come up */

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			      void *user_data, struct mbox_msg *data)
{
	struct ipc_ping_msg msg = {0};

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(msg)) {
		return;
	}
	memcpy(&msg, data->data, sizeof(msg));

	/* ISR-safe: don't touch I2C (LED) here, just enqueue */
	if (k_msgq_put(&rx_msgq, &msg, K_NO_WAIT) != 0) {
		g_msgq_drop_count++;
		if ((g_msgq_drop_count == 1u) || ((g_msgq_drop_count % 10u) == 0u)) {
			LOG_WRN("rx_msgq full, dropped=%u", g_msgq_drop_count);
		}
	}
}

static void toggle_task_entry(void *p1, void *p2, void *p3)
{
	struct ipc_ping_msg msg;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("[Toggle_Task] Started on M4");

	while (1) {
		struct ipc_ack_msg ack = {0};

		if (k_msgq_get(&rx_msgq, &msg, K_FOREVER) != 0) {
			continue;
		}

		if (led0_ready) {
			/* The only place LED0 (I2C) is touched: regular thread context */
			int ret = gpio_pin_toggle_dt(&led0);

			ack.led_ret = ret;
			LOG_INF("[Toggle_Task] ping seq=%u -> LED0 toggle ret=%d", msg.seq, ret);
		} else {
			ack.led_ret = -1000; /* led0 device not ready, as determined in main() */
			LOG_WRN("[Toggle_Task] ping seq=%u -> led0 not ready, skip toggle", msg.seq);
		}

		ack.seq = msg.seq;

		/* Diagnostic ack so the M55 console can see M4's status even without an M4 console */
		struct mbox_msg ack_mbox_msg = {.data = &ack, .size = sizeof(ack)};
		int send_ret = mbox_send_dt(&tx_channel, &ack_mbox_msg);

		if (send_ret < 0) {
			LOG_WRN("[Toggle_Task] ack send failed, ret=%d", send_ret);
		}
	}
}

int main(void)
{
	struct mbox_dt_spec rx_channel;

	LOG_INF("Lab01 Hello IPC CLIENT - %s", CONFIG_BOARD_TARGET);

	/*
	 * 2026-08-30: Do not return early here even if led0 fails to become
	 * ready/configured. The old code used to return immediately on
	 * failure, which meant the mbox callback never got registered at
	 * all — making it impossible to tell "IPC works but only the LED
	 * failed" apart from "M4 never even got this far" without an M4
	 * console. Track readiness with led0_ready instead so the IPC/ack
	 * path stays alive regardless, and the ack value on the M55 console
	 * alone can narrow down the cause.
	 */
	led0_ready = gpio_is_ready_dt(&led0);
	if (!led0_ready) {
		LOG_ERR("LED0 device not ready (continuing anyway, status reported to M55 via ack)");
	} else if (gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE) != 0) {
		LOG_ERR("LED0 configure failed (continuing anyway, status reported to M55 via ack)");
		led0_ready = false;
	}

	tx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);
	rx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

	if (mbox_register_callback_dt(&rx_channel, mbox_rx_callback, NULL)) {
		LOG_ERR("mbox_register_callback() failed");
		return -EIO;
	}
	if (mbox_set_enabled_dt(&rx_channel, 1)) {
		LOG_ERR("mbox_set_enabled() failed");
		return -EIO;
	}

	k_thread_create(&toggle_task_data, toggle_task_stack,
			K_THREAD_STACK_SIZEOF(toggle_task_stack),
			toggle_task_entry, NULL, NULL, NULL,
			TOGGLE_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&toggle_task_data, "Toggle_Task");

	LOG_INF("M4 ready, waiting for pings from M55.");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
