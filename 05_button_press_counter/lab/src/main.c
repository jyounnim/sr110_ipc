/*
 * Lab 05: Button Press Counter (HOST, M55)
 * Receives the running button-press total from M4 and just logs it.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab05_host, CONFIG_LOG_DEFAULT_LEVEL);

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			      void *user_data, struct mbox_msg *data)
{
	struct ipc_press_count_msg msg;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(msg)) {
		return;
	}
	memcpy(&msg, data->data, sizeof(msg));
	LOG_INF("total button presses so far: %u", msg.total_presses);
}

int main(void)
{
	struct mbox_dt_spec rx_channel;

	LOG_INF("Lab05 Button Press Counter HOST - %s", CONFIG_BOARD_TARGET);

	rx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

	if (mbox_register_callback_dt(&rx_channel, mbox_rx_callback, NULL)) {
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
