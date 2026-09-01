/*
 * Lab 07: Echo Service (CLIENT, M4)
 * Sends the received bytes straight back.
 *
 * IMPORTANT (2026-08-31, corrected after hardware testing): an earlier
 * version of this file called mbox_send_dt() directly from the rx
 * callback (ISR context), on the assumption that mbox_send_dt() is a
 * lightweight non-blocking register write. On real hardware this hung
 * the M4 core after a few messages -- on this board's mbox backend,
 * mbox_send_dt() can itself block (e.g. waiting for the previous message
 * to be drained), and blocking in ISR context is illegal and freezes the
 * core, exactly like the k_msleep()-in-ISR bug found in Lab 03. Because
 * the core froze inside the ISR, the deferred logging thread never got a
 * chance to run either, so even a diagnostic log placed before the
 * mbox_send_dt() call never made it to the console -- which is what
 * made this bug so confusing to observe.
 *
 * Conclusion: mbox_send_dt() must NOT be assumed non-blocking. Every mbox
 * rx callback -- with no exceptions -- must only enqueue to a k_msgq and
 * return; the actual mbox_send_dt() call (like any other potentially
 * blocking work) belongs in a separate worker thread. This lab now
 * follows the same ISR -> k_msgq -> worker thread pattern as every other
 * lab.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab07_client, CONFIG_LOG_DEFAULT_LEVEL);

#define ECHO_TASK_STACK_SIZE 1024
#define ECHO_TASK_PRIORITY   5

struct echo_item {
	uint8_t data[ECHO_MAX_LEN];
	size_t len;
};

K_THREAD_STACK_DEFINE(echo_task_stack, ECHO_TASK_STACK_SIZE);
static struct k_thread echo_task_data;

K_MSGQ_DEFINE(echo_msgq, sizeof(struct echo_item), 4, 4);

static struct mbox_dt_spec tx_channel;

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			      void *user_data, struct mbox_msg *data)
{
	struct echo_item item;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	item.len = MIN(data->size, sizeof(item.data));
	memcpy(item.data, data->data, item.len);

	/* ISR-safe: no blocking calls here, just enqueue for the worker thread */
	k_msgq_put(&echo_msgq, &item, K_NO_WAIT);
}

static void echo_task_entry(void *p1, void *p2, void *p3)
{
	struct echo_item item;
	struct mbox_msg reply;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("[Echo_Task] Started on M4");

	while (1) {
		if (k_msgq_get(&echo_msgq, &item, K_FOREVER) != 0) {
			continue;
		}

		LOG_INF("rx %u bytes, echoing back", (unsigned int)item.len);

		/* mbox_send_dt() may block on this board's mbox backend -- that's
		 * fine here, we're in thread context, not an ISR. */
		reply.data = item.data;
		reply.size = item.len;
		mbox_send_dt(&tx_channel, &reply);
	}
}

int main(void)
{
	struct mbox_dt_spec rx_channel;

	LOG_INF("Lab07 Echo Service CLIENT - %s", CONFIG_BOARD_TARGET);

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

	k_thread_create(&echo_task_data, echo_task_stack,
			K_THREAD_STACK_SIZEOF(echo_task_stack),
			echo_task_entry, NULL, NULL, NULL,
			ECHO_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&echo_task_data, "Echo_Task");

	LOG_INF("Echo service ready.");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
