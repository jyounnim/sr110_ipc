/*
 * Lab 06: Structured Command Message (HOST, M55)
 * Alternates sending SET_LED / GET_STATUS commands and logs M4's
 * GET_STATUS replies.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab06_host, CONFIG_LOG_DEFAULT_LEVEL);

#define STEP_PERIOD_MS 2000

static struct mbox_dt_spec tx_channel;

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			      void *user_data, struct mbox_msg *data)
{
	struct ipc_status_msg status;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(status)) {
		return;
	}
	memcpy(&status, data->data, sizeof(status));

	LOG_INF("STATUS <- led0=%u led1=%u button_presses=%u",
		status.led0_state, status.led1_state, status.button_press_count);
}

static void send_cmd(struct ipc_command_msg *cmd)
{
	struct mbox_msg mbox_msg = {.data = cmd, .size = sizeof(*cmd)};
	int ret = mbox_send_dt(&tx_channel, &mbox_msg);

	if (ret < 0) {
		LOG_WRN("mbox_send() failed, err=%d", ret);
	}
}

int main(void)
{
	struct mbox_dt_spec rx_channel;
	struct ipc_command_msg cmd;
	int step = 0;

	LOG_INF("Lab06 Structured Command HOST - %s", CONFIG_BOARD_TARGET);

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

	while (1) {
		switch (step % 5) {
		case 0:
			cmd = (struct ipc_command_msg){IPC_CMD_SET_LED, 0, 1};
			LOG_INF("CMD -> SET_LED led0=ON");
			break;
		case 1:
			cmd = (struct ipc_command_msg){IPC_CMD_SET_LED, 1, 1};
			LOG_INF("CMD -> SET_LED led1=ON");
			break;
		case 2:
			cmd = (struct ipc_command_msg){IPC_CMD_GET_STATUS, 0, 0};
			LOG_INF("CMD -> GET_STATUS");
			break;
		case 3:
			cmd = (struct ipc_command_msg){IPC_CMD_SET_LED, 0, 0};
			LOG_INF("CMD -> SET_LED led0=OFF");
			break;
		case 4:
			cmd = (struct ipc_command_msg){IPC_CMD_SET_LED, 1, 0};
			LOG_INF("CMD -> SET_LED led1=OFF");
			break;
		}
		send_cmd(&cmd);
		step++;
		k_msleep(STEP_PERIOD_MS);
	}

	return 0;
}
