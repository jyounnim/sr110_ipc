/*
 * Lab 08: Multi-Type Message Queue (HOST, M55)
 * Sends several LED pattern commands back-to-back (no delay in between)
 * to exercise M4's queuing behavior.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab08_host, CONFIG_LOG_DEFAULT_LEVEL);

static struct mbox_dt_spec tx_channel;

static void send_pattern(uint32_t pattern_id, uint32_t repeat)
{
	struct ipc_pattern_msg msg = {.pattern_id = pattern_id, .repeat = repeat};
	struct mbox_msg mbox_msg = {.data = &msg, .size = sizeof(msg)};

	mbox_send_dt(&tx_channel, &mbox_msg);
	LOG_INF("queued pattern_id=%u repeat=%u", pattern_id, repeat);
}

int main(void)
{
	LOG_INF("Lab08 Multi-Type Message Queue HOST - %s", CONFIG_BOARD_TARGET);

	tx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);

	while (1) {
		/* Send all 4 back-to-back -> they queue up on M4 while it's busy running them */
		send_pattern(PATTERN_BLINK_LED0, 3);
		send_pattern(PATTERN_BLINK_LED1, 3);
		send_pattern(PATTERN_ALTERNATE, 3);
		send_pattern(PATTERN_BOTH_FLASH, 2);

		k_msleep(6000);
	}

	return 0;
}
