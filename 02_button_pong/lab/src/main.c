/*
 * Lab 02: M4 -> M55 Button Pong (HOST, M55)
 *
 * M55 doesn't actively do anything here. It just logs to the console
 * whenever M4 sends a button event over mbox (LED1 hangs off M4's I2C GPIO
 * expander, so M55 can't drive it directly — see the HW ownership section
 * in chapter 0).
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab02_host, CONFIG_LOG_DEFAULT_LEVEL);

static struct mbox_dt_spec rx_channel;

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			      void *user_data, struct mbox_msg *data)
{
	struct ipc_button_evt evt;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(evt)) {
		return;
	}
	memcpy(&evt, data->data, sizeof(evt));

	/* Just a log — light enough to handle directly in the callback (ISR).
	 * (No need to hand this off to a worker thread unless there's
	 * blocking work like I2C involved.) */
	LOG_INF("Button event received, seq=%u", evt.seq);
}

int main(void)
{
	LOG_INF("Lab02 Button Pong HOST - %s", CONFIG_BOARD_TARGET);

	rx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

	if (mbox_register_callback_dt(&rx_channel, mbox_rx_callback, NULL)) {
		LOG_ERR("mbox_register_callback() failed");
		return -EIO;
	}
	if (mbox_set_enabled_dt(&rx_channel, 1)) {
		LOG_ERR("mbox_set_enabled() failed");
		return -EIO;
	}

	LOG_INF("Waiting for button events from M4.");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
