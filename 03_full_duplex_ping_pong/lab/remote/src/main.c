/*
 * Lab 03: Full-Duplex Ping-Pong (CLIENT, M4)
 *
 * On receiving a ping: (1) toggle LED0 in the worker thread, (2) reply with
 * pong using the same seq. LED0 sits behind I2C, so the toggle must happen
 * in the worker thread (never from the ISR).
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab03_client, CONFIG_LOG_DEFAULT_LEVEL);

#define PONG_TASK_STACK_SIZE 1024
#define PONG_TASK_PRIORITY   5

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

K_THREAD_STACK_DEFINE(pong_task_stack, PONG_TASK_STACK_SIZE);
static struct k_thread pong_task_data;

K_MSGQ_DEFINE(rx_msgq, sizeof(struct ipc_pingpong_msg), 4, 4);

static struct mbox_dt_spec tx_channel;

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
	k_msgq_put(&rx_msgq, &msg, K_NO_WAIT);
}

static void pong_task_entry(void *p1, void *p2, void *p3)
{
	struct ipc_pingpong_msg msg;
	struct mbox_msg mbox_msg;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("[Pong_Task] Started on M4");

	while (1) {
		if (k_msgq_get(&rx_msgq, &msg, K_FOREVER) != 0) {
			continue;
		}

		gpio_pin_toggle_dt(&led0);
		LOG_INF("PING received, seq=%u -> LED0 toggled, replying PONG", msg.seq);

		mbox_msg.data = &msg;
		mbox_msg.size = sizeof(msg);
		ret = mbox_send_dt(&tx_channel, &mbox_msg);
		if (ret < 0) {
			LOG_WRN("mbox_send() failed, err=%d", ret);
		}
	}
}

int main(void)
{
	struct mbox_dt_spec rx_channel;

	LOG_INF("Lab03 Full-Duplex Ping-Pong CLIENT - %s", CONFIG_BOARD_TARGET);

	if (!gpio_is_ready_dt(&led0)) {
		LOG_ERR("LED0 device not ready");
		return -ENODEV;
	}
	if (gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE) != 0) {
		LOG_ERR("LED0 configure failed");
		return -EIO;
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

	k_thread_create(&pong_task_data, pong_task_stack,
			K_THREAD_STACK_SIZEOF(pong_task_stack),
			pong_task_entry, NULL, NULL, NULL,
			PONG_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&pong_task_data, "Pong_Task");

	LOG_INF("M4 ready, waiting for pings.");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
