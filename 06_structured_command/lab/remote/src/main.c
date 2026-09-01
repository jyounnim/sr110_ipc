/*
 * Lab 06: Structured Command Message (CLIENT, M4)
 * SET_LED -> physically drives the given LED. GET_STATUS -> replies with the
 * current state. Button presses are also counted and included in the
 * GET_STATUS reply.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab06_client, CONFIG_LOG_DEFAULT_LEVEL);

#define CMD_TASK_STACK_SIZE 1024
#define CMD_TASK_PRIORITY   5

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

K_THREAD_STACK_DEFINE(cmd_task_stack, CMD_TASK_STACK_SIZE);
static struct k_thread cmd_task_data;

K_MSGQ_DEFINE(cmd_msgq, sizeof(struct ipc_command_msg), 4, 4);

static struct mbox_dt_spec tx_channel;
static uint32_t g_led0_state, g_led1_state, g_button_presses;

static void button_input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);
	if (evt->code == INPUT_KEY_0 && evt->value == 1) {
		g_button_presses++;
	}
}
INPUT_CALLBACK_DEFINE(NULL, button_input_cb, NULL);

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			      void *user_data, struct mbox_msg *data)
{
	struct ipc_command_msg cmd;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(cmd)) {
		return;
	}
	memcpy(&cmd, data->data, sizeof(cmd));
	k_msgq_put(&cmd_msgq, &cmd, K_NO_WAIT);
}

static void cmd_task_entry(void *p1, void *p2, void *p3)
{
	struct ipc_command_msg cmd;
	struct ipc_status_msg status;
	struct mbox_msg mbox_msg;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("[Cmd_Task] Started on M4");

	while (1) {
		if (k_msgq_get(&cmd_msgq, &cmd, K_FOREVER) != 0) {
			continue;
		}

		switch (cmd.cmd) {
		case IPC_CMD_SET_LED:
			if (cmd.led_id == 0) {
				gpio_pin_set_dt(&led0, cmd.led_state);
				g_led0_state = cmd.led_state;
			} else if (cmd.led_id == 1) {
				gpio_pin_set_dt(&led1, cmd.led_state);
				g_led1_state = cmd.led_state;
			}
			LOG_INF("SET_LED led_id=%u state=%u applied", cmd.led_id, cmd.led_state);
			break;

		case IPC_CMD_GET_STATUS:
			status.led0_state = g_led0_state;
			status.led1_state = g_led1_state;
			status.button_press_count = g_button_presses;

			mbox_msg.data = &status;
			mbox_msg.size = sizeof(status);
			mbox_send_dt(&tx_channel, &mbox_msg);
			LOG_INF("GET_STATUS replied");
			break;

		default:
			LOG_WRN("unknown cmd=%u", cmd.cmd);
			break;
		}
	}
}

int main(void)
{
	struct mbox_dt_spec rx_channel;

	LOG_INF("Lab06 Structured Command CLIENT - %s", CONFIG_BOARD_TARGET);

	if (!gpio_is_ready_dt(&led0) || !gpio_is_ready_dt(&led1)) {
		LOG_ERR("LED device(s) not ready");
		return -ENODEV;
	}
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);

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

	k_thread_create(&cmd_task_data, cmd_task_stack,
			K_THREAD_STACK_SIZEOF(cmd_task_stack),
			cmd_task_entry, NULL, NULL, NULL,
			CMD_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&cmd_task_data, "Cmd_Task");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
