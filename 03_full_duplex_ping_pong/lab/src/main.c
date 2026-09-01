/*
 * Lab 03: Full-Duplex Ping-Pong (HOST, M55)
 *
 * Sends the first ping to M4 at boot. From then on, every time a pong
 * (reply) comes back from M4, seq is incremented and the next ping is sent
 * right away — forming a continuous round-trip loop.
 * (Labs 01/02 only used one direction; here both the tx and rx channels
 * are used at the same time.)
 *
 * Important: the mbox rx callback runs in ISR context, so it must never
 * block (e.g. k_msleep()). The callback only enqueues the received pong;
 * the actual delay + next-ping send happens in a separate worker thread —
 * the same ISR -> k_msgq -> worker thread pattern used on the M4 side.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab03_host, CONFIG_LOG_DEFAULT_LEVEL);

#define PINGPONG_DELAY_MS   500
#define PONG_TASK_STACK_SIZE 1024
#define PONG_TASK_PRIORITY   5

K_THREAD_STACK_DEFINE(pong_task_stack, PONG_TASK_STACK_SIZE);
static struct k_thread pong_task_data;

K_MSGQ_DEFINE(rx_msgq, sizeof(struct ipc_pingpong_msg), 4, 4);

static struct mbox_dt_spec tx_channel;

static void send_ping(uint32_t seq)
{
	struct ipc_pingpong_msg msg = {.seq = seq};
	struct mbox_msg mbox_msg = {.data = &msg, .size = sizeof(msg)};
	int ret;

	ret = mbox_send_dt(&tx_channel, &mbox_msg);
	if (ret < 0) {
		LOG_WRN("mbox_send() failed, err=%d", ret);
	} else {
		LOG_INF("PING sent, seq=%u", seq);
	}
}

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			      void *user_data, struct mbox_msg *data)
{
	struct ipc_pingpong_msg msg;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(msg)) {
		return;
	}
	memcpy(&msg, data->data, sizeof(msg));

	/* ISR-safe: no blocking calls here, just enqueue for the worker thread */
	k_msgq_put(&rx_msgq, &msg, K_NO_WAIT);
}

static void pong_task_entry(void *p1, void *p2, void *p3)
{
	struct ipc_pingpong_msg msg;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		if (k_msgq_get(&rx_msgq, &msg, K_FOREVER) != 0) {
			continue;
		}

		LOG_INF("PONG received, seq=%u", msg.seq);

		/* Pause here (thread context — perfectly fine to block) so the
		 * ping-pong rhythm is easy to watch on the console/LED. */
		k_msleep(PINGPONG_DELAY_MS);
		send_ping(msg.seq + 1);
	}
}

int main(void)
{
	struct mbox_dt_spec rx_channel;

	LOG_INF("Lab03 Full-Duplex Ping-Pong HOST - %s", CONFIG_BOARD_TARGET);

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

	k_thread_create(&pong_task_data, pong_task_stack,
			K_THREAD_STACK_SIZEOF(pong_task_stack),
			pong_task_entry, NULL, NULL, NULL,
			PONG_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&pong_task_data, "Pong_Task");

	k_msleep(500); /* give M4 a moment to finish registering its callback first */
	send_ping(1);

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
