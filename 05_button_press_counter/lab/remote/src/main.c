/*
 * Lab 05: Button Press Counter (CLIENT, M4)
 * Every time user_button is pressed, increments a running total and sends
 * it to M55.
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab05_client, CONFIG_LOG_DEFAULT_LEVEL);

#define SEND_TASK_STACK_SIZE 1024
#define SEND_TASK_PRIORITY   5

K_THREAD_STACK_DEFINE(send_task_stack, SEND_TASK_STACK_SIZE);
static struct k_thread send_task_data;

K_MSGQ_DEFINE(press_msgq, sizeof(uint8_t), 4, 4);

static struct mbox_dt_spec tx_channel;
static uint32_t g_total_presses;

static void button_input_cb(struct input_event *evt, void *user_data)
{
	uint8_t dummy = 1;

	ARG_UNUSED(user_data);

	if (evt->code != INPUT_KEY_0 || evt->value != 1) {
		return;
	}
	k_msgq_put(&press_msgq, &dummy, K_NO_WAIT);
}
INPUT_CALLBACK_DEFINE(NULL, button_input_cb, NULL);

static void send_task_entry(void *p1, void *p2, void *p3)
{
	uint8_t dummy;
	struct ipc_press_count_msg msg;
	struct mbox_msg mbox_msg;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("[Send_Task] Started on M4");

	while (1) {
		if (k_msgq_get(&press_msgq, &dummy, K_FOREVER) != 0) {
			continue;
		}

		g_total_presses++;
		msg.total_presses = g_total_presses;

		mbox_msg.data = &msg;
		mbox_msg.size = sizeof(msg);
		ret = mbox_send_dt(&tx_channel, &mbox_msg);
		if (ret < 0) {
			LOG_WRN("mbox_send() failed, err=%d", ret);
		} else {
			LOG_INF("button press #%u sent to M55", g_total_presses);
		}
	}
}

int main(void)
{
	LOG_INF("Lab05 Button Press Counter CLIENT - %s", CONFIG_BOARD_TARGET);

	tx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);

	k_thread_create(&send_task_data, send_task_stack,
			K_THREAD_STACK_SIZEOF(send_task_stack),
			send_task_entry, NULL, NULL, NULL,
			SEND_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&send_task_data, "Send_Task");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
