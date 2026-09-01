/*
 * Lab 10: Threshold Event Notification (HOST, M55)
 * Sets the threshold on M4 once at startup, then only receives/logs
 * events that actually exceed it.
 * (Unlike Lab 09's "send every sample", events only arrive when the
 * condition is met.)
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab10_host, CONFIG_LOG_DEFAULT_LEVEL);

/* Raised from 1200 to 2000 milli-g after hardware testing (2026-08-31):
 * even after boot-time baseline calibration, the sensor's residual noise
 * on this board reached as high as mag=1515 while sitting still, so 1200
 * kept firing false events. 2000 gives a comfortable margin above that. */
#define ACCEL_THRESHOLD_MILLI_G 2000

static void rx_cb(const struct device *dev, mbox_channel_id_t channel_id,
		   void *user_data, struct mbox_msg *data)
{
	struct ipc10_event_msg msg;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(msg)) {
		return;
	}
	memcpy(&msg, data->data, sizeof(msg));

	LOG_WRN("THRESHOLD EVENT seq=%u x=%d y=%d z=%d", msg.seq, msg.x, msg.y, msg.z);
}

int main(void)
{
	struct mbox_dt_spec tx_channel, rx_channel;
	struct ipc10_cmd_msg cmd = {.cmd = IPC10_CMD_SET_THRESHOLD,
				     .threshold = ACCEL_THRESHOLD_MILLI_G};

	LOG_INF("Lab10 Threshold Event HOST - %s", CONFIG_BOARD_TARGET);

	tx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);
	rx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

	if (mbox_register_callback_dt(&rx_channel, rx_cb, NULL)) {
		LOG_ERR("mbox_register_callback() failed");
		return -EIO;
	}
	if (mbox_set_enabled_dt(&rx_channel, 1)) {
		LOG_ERR("mbox_set_enabled() failed");
		return -EIO;
	}

	k_msleep(200); /* give M4 a moment to finish registering its rx callback first */

	struct mbox_msg mbox_msg = {.data = &cmd, .size = sizeof(cmd)};
	mbox_send_dt(&tx_channel, &mbox_msg);
	LOG_INF("threshold set to %d milli-g", cmd.threshold);

	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
