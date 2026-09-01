/*
 * Lab 10: Threshold Event Notification (CLIENT, M4)
 * Polls the accelerometer every 100ms; sends an event only when the
 * deviation from a boot-time baseline exceeds the threshold (debounced).
 * The SET_THRESHOLD command is also handled by the same worker thread,
 * via a k_msgq timeout.
 *
 * Node label/status and driver wake-up requirements (all confirmed on
 * real hardware during Lab 09, 2026-08-31): the accelerometer node is
 * "mc3479" (not "accel0"), defaults to status = "disabled" in the base
 * dts and must be re-enabled in the M4 overlay, and the driver requires
 * BOTH sensor_attr_set(SENSOR_ATTR_SAMPLING_FREQUENCY) (to wake it from
 * standby) AND sensor_attr_set(SENSOR_ATTR_FULL_SCALE) (to populate its
 * internal sensitivity factor -- without it every reading comes out as
 * raw_value * 0 = 0) before sample_fetch()/channel_get() return real
 * data. See Lab 09 README for the full diagnosis.
 *
 * Calibration (2026-08-31, added after Lab 10 hardware testing): the
 * uncalibrated sensor reads out roughly 1g on whichever axis is aligned
 * with gravity (e.g. z=-10000ish milli-units in the boot-time orientation
 * used during testing) even sitting perfectly still, which alone was
 * larger than the threshold -- so the board looked like it was
 * continuously "moving" from the moment it booted. Rather than trying to
 * hand-tune the threshold around an arbitrary boot orientation, main()
 * now samples the sensor for about a second right after boot, averages
 * X/Y/Z, and stores that as a per-axis baseline (this is done on M4,
 * since M4 is the one physically driving the sensor). Every later sample
 * is compared against this baseline instead of raw zero, so only actual
 * *changes* in orientation/motion -- not the constant 1g offset -- can
 * trigger an event. This baseline is a fixed snapshot taken once at
 * boot, not a running/adaptive filter; if the board is moved during that
 * initial ~1s window, the baseline itself will be off.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab10_client, CONFIG_LOG_DEFAULT_LEVEL);

#define TASK_STACK_SIZE   1024
#define TASK_PRIORITY     4
#define POLL_PERIOD_MS    100
#define DEBOUNCE_MS       500
#define CALIB_SAMPLES     10  /* ~1s of samples at POLL_PERIOD_MS each */

K_THREAD_STACK_DEFINE(task_stack, TASK_STACK_SIZE);
static struct k_thread task_data;
K_MSGQ_DEFINE(cmd_msgq, sizeof(struct ipc10_cmd_msg), 4, 4);

static struct mbox_dt_spec tx_channel;
static const struct device *accel_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(mc3479));
static int32_t threshold_milli_g = 2000; /* default (M55 overwrites via SET_THRESHOLD shortly after boot) */

static void rx_cb(const struct device *dev, mbox_channel_id_t channel_id,
		   void *user_data, struct mbox_msg *data)
{
	struct ipc10_cmd_msg msg;

	ARG_UNUSED(dev);
	ARG_UNUSED(channel_id);
	ARG_UNUSED(user_data);

	if (data->size < sizeof(msg)) {
		return;
	}
	memcpy(&msg, data->data, sizeof(msg));
	k_msgq_put(&cmd_msgq, &msg, K_NO_WAIT);
}

static int32_t sensor_val_to_milli(struct sensor_value *v)
{
	return v->val1 * 1000 + v->val2 / 1000;
}

/* Fetches one X/Y/Z sample in milli-units. Returns 0 on success. */
static int read_accel_milli(int32_t *x, int32_t *y, int32_t *z)
{
	struct sensor_value ax, ay, az;

	if (accel_dev == NULL || !device_is_ready(accel_dev)) {
		return -ENODEV;
	}
	if (sensor_sample_fetch(accel_dev) != 0) {
		return -EIO;
	}
	sensor_channel_get(accel_dev, SENSOR_CHAN_ACCEL_X, &ax);
	sensor_channel_get(accel_dev, SENSOR_CHAN_ACCEL_Y, &ay);
	sensor_channel_get(accel_dev, SENSOR_CHAN_ACCEL_Z, &az);

	*x = sensor_val_to_milli(&ax);
	*y = sensor_val_to_milli(&ay);
	*z = sensor_val_to_milli(&az);
	return 0;
}

/* Samples the sensor for ~1s right after boot and averages the result,
 * to use as a per-axis baseline (cancels out the constant ~1g gravity
 * offset plus whatever fixed tilt the board happens to be sitting at). */
static void calibrate_baseline(int32_t *base_x, int32_t *base_y, int32_t *base_z)
{
	int64_t sum_x = 0, sum_y = 0, sum_z = 0;
	int good_samples = 0;
	int32_t x, y, z;

	LOG_INF("[Threshold_Task] calibrating baseline (~1s, keep the board still)...");

	for (int i = 0; i < CALIB_SAMPLES; i++) {
		if (read_accel_milli(&x, &y, &z) == 0) {
			sum_x += x;
			sum_y += y;
			sum_z += z;
			good_samples++;
		}
		k_msleep(POLL_PERIOD_MS);
	}

	if (good_samples == 0) {
		*base_x = 0;
		*base_y = 0;
		*base_z = 0;
		LOG_WRN("[Threshold_Task] calibration got no valid samples, baseline=0,0,0");
		return;
	}

	*base_x = (int32_t)(sum_x / good_samples);
	*base_y = (int32_t)(sum_y / good_samples);
	*base_z = (int32_t)(sum_z / good_samples);
	LOG_INF("[Threshold_Task] baseline x=%d y=%d z=%d (from %d samples)",
		*base_x, *base_y, *base_z, good_samples);
}

static void task_entry(void *p1, void *p2, void *p3)
{
	struct ipc10_cmd_msg cmd;
	struct ipc_uptime {
		int64_t last_event_ms;
	} state = {.last_event_ms = 0};
	uint32_t seq = 0;
	int32_t base_x, base_y, base_z;

	LOG_INF("[Threshold_Task] started, default threshold=%d", threshold_milli_g);

	calibrate_baseline(&base_x, &base_y, &base_z);

	while (1) {
		/* Process a command if one is waiting; otherwise time out after
		 * POLL_PERIOD_MS and treat that as the sampling tick */
		if (k_msgq_get(&cmd_msgq, &cmd, K_MSEC(POLL_PERIOD_MS)) == 0) {
			if (cmd.cmd == IPC10_CMD_SET_THRESHOLD) {
				threshold_milli_g = cmd.threshold;
				LOG_INF("[Threshold_Task] threshold updated to %d", threshold_milli_g);
			}
			continue;
		}

		int32_t x, y, z;

		if (read_accel_milli(&x, &y, &z) != 0) {
			continue;
		}

		/* Compare the deviation from the boot-time baseline, not the raw
		 * reading -- the raw reading always carries ~1g from gravity plus
		 * whatever fixed tilt the board sits at, which would otherwise
		 * exceed any reasonable threshold even standing still. */
		int32_t dx = x - base_x;
		int32_t dy = y - base_y;
		int32_t dz = z - base_z;
		int32_t mag = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + (dz < 0 ? -dz : dz);

		int64_t now = k_uptime_get();
		if (mag > threshold_milli_g && (now - state.last_event_ms) > DEBOUNCE_MS) {
			struct ipc10_event_msg evt = {.x = x, .y = y, .z = z, .seq = ++seq};
			struct mbox_msg mbox_msg = {.data = &evt, .size = sizeof(evt)};

			mbox_send_dt(&tx_channel, &mbox_msg);
			state.last_event_ms = now;
			LOG_INF("[Threshold_Task] event sent seq=%u mag=%d (dev x=%d y=%d z=%d)",
				seq, mag, dx, dy, dz);
		}
	}
}

int main(void)
{
	struct mbox_dt_spec rx_channel;

	LOG_INF("Lab10 Threshold Event CLIENT - %s", CONFIG_BOARD_TARGET);

	tx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);
	rx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), rx);

	if (mbox_register_callback_dt(&rx_channel, rx_cb, NULL)) {
		LOG_ERR("mbox_register_callback() failed");
		return -EIO;
	}
	if (mbox_set_enabled_dt(&rx_channel, 1)) {
		LOG_ERR("mbox_set_enabled() failed");
		return -EIO;
	}

	if (accel_dev == NULL || !device_is_ready(accel_dev)) {
		LOG_ERR("accel device not ready (check DT_NODELABEL(mc3479))");
	} else {
		/* Wake the sensor (ODR) and populate its internal sensitivity
		 * factor (full-scale range) -- both are required, see Lab 09. */
		struct sensor_value odr = {.val1 = 50, .val2 = 0}; /* 50 Hz */
		struct sensor_value range = {.val1 = 0, .val2 = 0}; /* smallest/most sensitive */

		if (sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ,
				     SENSOR_ATTR_SAMPLING_FREQUENCY, &odr) != 0) {
			LOG_ERR("sensor_attr_set(SAMPLING_FREQUENCY) failed -- accel may stay in standby");
		}
		if (sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ,
				     SENSOR_ATTR_FULL_SCALE, &range) != 0) {
			LOG_ERR("sensor_attr_set(FULL_SCALE) failed -- readings may stay at 0");
		}
	}

	k_thread_create(&task_data, task_stack, K_THREAD_STACK_SIZEOF(task_stack),
			task_entry, NULL, NULL, NULL, TASK_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&task_data, "Threshold_Task");

	while (1) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
