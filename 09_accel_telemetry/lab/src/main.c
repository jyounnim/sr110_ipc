/*
 * Lab 09: Accelerometer Telemetry (HOST, M55)
 * Receives the X/Y/Z values M4 periodically samples from the onboard
 * MC3419 accelerometer and logs them.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab09_host, CONFIG_LOG_DEFAULT_LEVEL);

static void rx_cb(const struct device *dev, mbox_channel_id_t channel_id,
		   void *user_data, struct mbox_msg *data)
{
	struct ipc_accel_msg msg;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(msg)) {
		return;
	}
	memcpy(&msg, data->data, sizeof(msg));

	/* Handled directly in the ISR callback since this only logs -- a plain
	 * LOG_INF() call is not a blocking call (see Lab 03/07: only an
	 * explicit blocking call like k_msleep(), or mbox_send_dt() itself on
	 * this board, is disallowed here; there is no mbox_send_dt() call in
	 * this callback at all). */
	LOG_INF("accel seq=%u x=%d y=%d z=%d", msg.seq, msg.x, msg.y, msg.z);
}

int main(void)
{
	struct mbox_dt_spec rx_channel;

	LOG_INF("Lab09 Accel Telemetry HOST - %s", CONFIG_BOARD_TARGET);

	rx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

	if (mbox_register_callback_dt(&rx_channel, rx_cb, NULL)) {
		LOG_ERR("mbox_register_callback() failed");
		return -EIO;
	}
	if (mbox_set_enabled_dt(&rx_channel, 1)) {
		LOG_ERR("mbox_set_enabled() failed");
		return -EIO;
	}

	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
