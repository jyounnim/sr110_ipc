/*
 * Lab 04: Shared Counter (HOST, M55)
 * Increments a counter every second and sends it to M4.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab04_host, CONFIG_LOG_DEFAULT_LEVEL);

#define TICK_PERIOD_MS 1000

int main(void)
{
	struct mbox_dt_spec tx_channel;
	struct ipc_counter_msg msg = {.counter = 0};
	struct mbox_msg mbox_msg;
	int ret;

	LOG_INF("Lab04 Shared Counter HOST - %s", CONFIG_BOARD_TARGET);

	tx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);

	while (1) {
		msg.counter++;

		mbox_msg.data = &msg;
		mbox_msg.size = sizeof(msg);
		ret = mbox_send_dt(&tx_channel, &mbox_msg);
		if (ret < 0) {
			LOG_WRN("mbox_send() failed, err=%d", ret);
		} else {
			LOG_INF("counter=%u sent", msg.counter);
		}

		k_msleep(TICK_PERIOD_MS);
	}

	return 0;
}
