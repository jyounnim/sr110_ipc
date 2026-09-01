/*
 * Lab 02: M4 -> M55 Button Pong (CLIENT, M4)
 *
 * When user_button (SW8, gpio-keys via gpio_exp0) is pressed:
 *   1. Toggle the local LED1 (immediate visual feedback)
 *   2. Send an event to M55 over mbox
 *
 * The Zephyr Input subsystem's (zephyr/input/input.h) callback also runs in
 * a near-interrupt context, so the LED1 toggle (I2C) and mbox_send_dt() are
 * both handled in a worker thread (same ISR/event -> k_msgq -> worker
 * thread pattern as Lab 01).
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab02_client, CONFIG_LOG_DEFAULT_LEVEL);

#define BUTTON_TASK_STACK_SIZE 1024
#define BUTTON_TASK_PRIORITY   5

static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

K_THREAD_STACK_DEFINE(button_task_stack, BUTTON_TASK_STACK_SIZE);
static struct k_thread button_task_data;

/* Only the press event itself is queued (no payload — its presence is the signal) */
K_MSGQ_DEFINE(press_msgq, sizeof(uint8_t), 4, 4);

static struct mbox_dt_spec tx_channel;
static uint32_t g_press_count;

static void button_input_cb(struct input_event *evt, void *user_data)
{
	uint8_t dummy = 1;

	ARG_UNUSED(user_data);

	/* Only handle press (1), ignore release (0) */
	if (evt->code != INPUT_KEY_0 || evt->value != 1) {
		return;
	}

	k_msgq_put(&press_msgq, &dummy, K_NO_WAIT);
}
INPUT_CALLBACK_DEFINE(NULL, button_input_cb, NULL);

static void button_task_entry(void *p1, void *p2, void *p3)
{
	uint8_t dummy;
	struct ipc_button_evt out_evt;
	struct mbox_msg mbox_msg;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("[Button_Task] Started on M4");

	while (1) {
		if (k_msgq_get(&press_msgq, &dummy, K_FOREVER) != 0) {
			continue;
		}

		g_press_count++;

		/* Visual feedback: toggle local LED1 (I2C, safe here since we're in thread context) */
		gpio_pin_toggle_dt(&led1);

		/* Send the event to M55 */
		out_evt.seq = g_press_count;
		mbox_msg.data = &out_evt;
		mbox_msg.size = sizeof(out_evt);

		ret = mbox_send_dt(&tx_channel, &mbox_msg);
		if (ret < 0) {
			LOG_WRN("mbox_send() failed, err=%d", ret);
		} else {
			LOG_INF("[Button_Task] button pressed, seq=%u -> sent to M55", g_press_count);
		}
	}
}

int main(void)
{
	LOG_INF("Lab02 Button Pong CLIENT - %s", CONFIG_BOARD_TARGET);

	if (!gpio_is_ready_dt(&led1)) {
		LOG_ERR("LED1 device not ready");
		return -ENODEV;
	}
	if (gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE) != 0) {
		LOG_ERR("LED1 configure failed");
		return -EIO;
	}

	tx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);

	k_thread_create(&button_task_data, button_task_stack,
			K_THREAD_STACK_SIZEOF(button_task_stack),
			button_task_entry, NULL, NULL, NULL,
			BUTTON_TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&button_task_data, "Button_Task");

	LOG_INF("M4 ready, waiting for button presses.");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
