/*
 * Lab 08: Multi-Type Message Queue (CLIENT, M4)
 * Runs each queued LED pattern command to completion (blocking), one at a
 * time, in the order received. The k_msgq depth is bumped to 8 so all 4
 * commands M55 sends back-to-back can be buffered.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab08_client, CONFIG_LOG_DEFAULT_LEVEL);

#define PATTERN_TASK_STACK_SIZE 1024
#define PATTERN_TASK_PRIORITY   5
#define STEP_MS 200

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

K_THREAD_STACK_DEFINE(pattern_task_stack, PATTERN_TASK_STACK_SIZE);
static struct k_thread pattern_task_data;

K_MSGQ_DEFINE(pattern_msgq, sizeof(struct ipc_pattern_msg), 8, 4);

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			      void *user_data, struct mbox_msg *data)
{
	struct ipc_pattern_msg msg;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(msg)) {
		return;
	}
	memcpy(&msg, data->data, sizeof(msg));
	k_msgq_put(&pattern_msgq, &msg, K_NO_WAIT);
}

static void run_pattern(const struct ipc_pattern_msg *msg)
{
	switch (msg->pattern_id) {
	case PATTERN_BLINK_LED0:
		for (uint32_t i = 0; i < msg->repeat; i++) {
			gpio_pin_set_dt(&led0, 1);
			k_msleep(STEP_MS);
			gpio_pin_set_dt(&led0, 0);
			k_msleep(STEP_MS);
		}
		break;
	case PATTERN_BLINK_LED1:
		for (uint32_t i = 0; i < msg->repeat; i++) {
			gpio_pin_set_dt(&led1, 1);
			k_msleep(STEP_MS);
			gpio_pin_set_dt(&led1, 0);
			k_msleep(STEP_MS);
		}
		break;
	case PATTERN_ALTERNATE:
		for (uint32_t i = 0; i < msg->repeat; i++) {
			gpio_pin_set_dt(&led0, 1);
			gpio_pin_set_dt(&led1, 0);
			k_msleep(STEP_MS);
			gpio_pin_set_dt(&led0, 0);
			gpio_pin_set_dt(&led1, 1);
			k_msleep(STEP_MS);
		}
		gpio_pin_set_dt(&led1, 0);
		break;
	case PATTERN_BOTH_FLASH:
		for (uint32_t i = 0; i < msg->repeat; i++) {
			gpio_pin_set_dt(&led0, 1);
			gpio_pin_set_dt(&led1, 1);
			k_msleep(STEP_MS);
			gpio_pin_set_dt(&led0, 0);
			gpio_pin_set_dt(&led1, 0);
			k_msleep(STEP_MS);
		}
		break;
	default:
		LOG_WRN("unknown pattern_id=%u", msg->pattern_id);
		break;
	}
}

static void pattern_task_entry(void *p1, void *p2, void *p3)
{
	struct ipc_pattern_msg msg;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("[Pattern_Task] Started on M4");

	while (1) {
		if (k_msgq_get(&pattern_msgq, &msg, K_FOREVER) != 0) {
			continue;
		}
		LOG_INF("running pattern_id=%u repeat=%u (queue depth now=%u)",
			msg.pattern_id, msg.repeat, k_msgq_num_used_get(&pattern_msgq));
		run_pattern(&msg);
	}
}

int main(void)
{
	struct mbox_dt_spec rx_channel;

	LOG_INF("Lab08 Multi-Type Message Queue CLIENT - %s", CONFIG_BOARD_TARGET);

	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);

	rx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

	if (mbox_register_callback_dt(&rx_channel, mbox_rx_callback, NULL)) {
		LOG_ERR("mbox_register_callback() failed");
		return -EIO;
	}
	if (mbox_set_enabled_dt(&rx_channel, 1)) {
		LOG_ERR("mbox_set_enabled() failed");
		return -EIO;
	}

	k_thread_create(&pattern_task_data, pattern_task_stack,
			K_THREAD_STACK_SIZEOF(pattern_task_stack),
			pattern_task_entry, NULL, NULL, NULL,
			PATTERN_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&pattern_task_data, "Pattern_Task");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
