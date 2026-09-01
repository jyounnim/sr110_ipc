/*
 * Lab 04: Shared Counter (CLIENT, M4)
 * "Displays" the received counter value by blinking LED0 (counter % 5 + 1) times.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab04_client, CONFIG_LOG_DEFAULT_LEVEL);

#define BLINK_TASK_STACK_SIZE 1024
#define BLINK_TASK_PRIORITY   5
#define BLINK_ON_MS  150
#define BLINK_OFF_MS 150

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

K_THREAD_STACK_DEFINE(blink_task_stack, BLINK_TASK_STACK_SIZE);
static struct k_thread blink_task_data;

K_MSGQ_DEFINE(rx_msgq, sizeof(struct ipc_counter_msg), 4, 4);

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			      void *user_data, struct mbox_msg *data)
{
	struct ipc_counter_msg msg;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(msg)) {
		return;
	}
	memcpy(&msg, data->data, sizeof(msg));
	k_msgq_put(&rx_msgq, &msg, K_NO_WAIT);
}

static void blink_task_entry(void *p1, void *p2, void *p3)
{
	struct ipc_counter_msg msg;
	uint32_t blink_count;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("[Blink_Task] Started on M4");

	while (1) {
		if (k_msgq_get(&rx_msgq, &msg, K_FOREVER) != 0) {
			continue;
		}

		blink_count = (msg.counter % 5U) + 1U;
		LOG_INF("counter=%u received -> blinking LED0 %u times", msg.counter, blink_count);

		for (uint32_t i = 0; i < blink_count; i++) {
			gpio_pin_set_dt(&led0, 1);
			k_msleep(BLINK_ON_MS);
			gpio_pin_set_dt(&led0, 0);
			k_msleep(BLINK_OFF_MS);
		}
	}
}

int main(void)
{
	struct mbox_dt_spec rx_channel;

	LOG_INF("Lab04 Shared Counter CLIENT - %s", CONFIG_BOARD_TARGET);

	if (!gpio_is_ready_dt(&led0)) {
		LOG_ERR("LED0 device not ready");
		return -ENODEV;
	}
	if (gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE) != 0) {
		LOG_ERR("LED0 configure failed");
		return -EIO;
	}

	rx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

	if (mbox_register_callback_dt(&rx_channel, mbox_rx_callback, NULL)) {
		LOG_ERR("mbox_register_callback() failed");
		return -EIO;
	}
	if (mbox_set_enabled_dt(&rx_channel, 1)) {
		LOG_ERR("mbox_set_enabled() failed");
		return -EIO;
	}

	k_thread_create(&blink_task_data, blink_task_stack,
			K_THREAD_STACK_SIZEOF(blink_task_stack),
			blink_task_entry, NULL, NULL, NULL,
			BLINK_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&blink_task_data, "Blink_Task");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
