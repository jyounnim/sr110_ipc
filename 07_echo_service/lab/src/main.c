/*
 * Lab 07: Echo Service (HOST, M55)
 * Sends a string to M4 and checks whether the echoed reply matches the
 * original.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab07_host, CONFIG_LOG_DEFAULT_LEVEL);

static const char *const test_strings[] = {
	"hello",
	"SR110 M4/M55 IPC",
	"echo test 123",
};
#define NUM_TEST_STRINGS ARRAY_SIZE(test_strings)

static char g_last_sent[ECHO_MAX_LEN];

static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
			      void *user_data, struct mbox_msg *data)
{
	char buf[ECHO_MAX_LEN] = {0};
	size_t len = MIN(data->size, sizeof(buf) - 1);

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	memcpy(buf, data->data, len);

	if (strcmp(buf, g_last_sent) == 0) {
		LOG_INF("ECHO OK: \"%s\"", buf);
	} else {
		LOG_WRN("ECHO MISMATCH: sent=\"%s\" got=\"%s\"", g_last_sent, buf);
	}
}

int main(void)
{
	struct mbox_dt_spec tx_channel, rx_channel;
	struct mbox_msg mbox_msg;
	size_t idx = 0;

	LOG_INF("Lab07 Echo Service HOST - %s", CONFIG_BOARD_TARGET);

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
		strncpy(g_last_sent, test_strings[idx % NUM_TEST_STRINGS], sizeof(g_last_sent) - 1);
		g_last_sent[sizeof(g_last_sent) - 1] = '\0';

		mbox_msg.data = g_last_sent;
		mbox_msg.size = strlen(g_last_sent) + 1; /* +1 to include the NUL terminator */

		LOG_INF("Sending: \"%s\"", g_last_sent);
		mbox_send_dt(&tx_channel, &mbox_msg);

		idx++;
		k_msleep(2000);
	}

	return 0;
}
