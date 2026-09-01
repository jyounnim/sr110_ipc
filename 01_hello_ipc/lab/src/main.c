/*
 * Lab 01: Hello IPC — M55 -> M4 Ping (HOST, built for M55)
 *
 * Sends a ping message to M4 once per second. This lab does not wait for a
 * reply from M4 (one-way signal check only). Bidirectional comms are
 * covered in Lab 03.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab01_host, CONFIG_LOG_DEFAULT_LEVEL);

/* Ping send period (ms) */
#define PING_PERIOD_MS 1000

/*
 * 2026-08-30: Temporary ack-receive callback added while the M4 physical
 * console was not yet available. Lets the M55 console alone confirm whether
 * M4 received the ping and whether the LED0 write succeeded (see struct
 * ipc_ack_msg in ipc_common.h). Handled directly in the ISR callback since
 * it's just a log (no blocking work like I2C here).
 */
static void mbox_ack_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
				  void *user_data, struct mbox_msg *data)
{
	struct ipc_ack_msg ack;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(ack)) {
		return;
	}
	memcpy(&ack, data->data, sizeof(ack));

	if (ack.led_ret == 0) {
		LOG_INF("[ACK] seq=%u M4 alive, LED0 toggle OK", ack.seq);
	} else if (ack.led_ret == -1000) {
		LOG_WRN("[ACK] seq=%u M4 alive, but LED0 device NOT READY", ack.seq);
	} else {
		LOG_WRN("[ACK] seq=%u M4 alive, LED0 toggle FAILED ret=%d", ack.seq, ack.led_ret);
	}
}

int main(void)
{
	struct mbox_dt_spec tx_channel; /* mbox channel used to send to M4 */
	struct mbox_dt_spec rx_channel; /* mbox channel used to receive acks from M4 (diagnostic, added 2026-08-30) */
	struct ipc_ping_msg msg = {.seq = 0};
	struct mbox_msg mbox_msg;
	int mtu;
	int ret;

	LOG_INF("Lab01 Hello IPC HOST - %s", CONFIG_BOARD_TARGET);

	tx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);
	rx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

	/* Catch an MTU-too-small-for-our-message error right at boot. */
	mtu = mbox_mtu_get_dt(&tx_channel);
	if (mtu < (int)sizeof(msg)) {
		LOG_ERR("mbox MTU %d too small for ping message (%zu bytes)", mtu, sizeof(msg));
		return -EINVAL;
	}

	if (mbox_register_callback_dt(&rx_channel, mbox_ack_rx_callback, NULL)) {
		LOG_ERR("mbox_register_callback() (ack) failed");
		return -EIO;
	}
	if (mbox_set_enabled_dt(&rx_channel, 1)) {
		LOG_ERR("mbox_set_enabled() (ack) failed");
		return -EIO;
	}

	LOG_INF("Sending ping to M4 every %d ms", PING_PERIOD_MS);

	while (1) {
		msg.seq++;

		mbox_msg.data = &msg;
		mbox_msg.size = sizeof(msg);

		ret = mbox_send_dt(&tx_channel, &mbox_msg);
		if (ret < 0) {
			LOG_WRN("mbox_send() failed, err=%d", ret);
		} else {
			LOG_INF("Ping sent, seq=%u", msg.seq);
		}

		k_msleep(PING_PERIOD_MS);
	}

	return 0;
}
