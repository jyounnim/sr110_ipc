/*
 * Lab 09: Accelerometer Telemetry (CLIENT, M4)
 * Reads the onboard MC3419 accelerometer every 500ms and sends the values
 * to M55.
 *
 * Confirmed against the real board dts (2026-08-31): the accelerometer
 * node is labeled "mc3479" (compatible = "memsic,mc3419", on i2c1 @ 0x4c),
 * not "accel0" as originally guessed -- and it defaults to
 * status = "disabled" in the base board dts, which had to be re-enabled
 * in the M4 overlay (see remote/boards/sr100_rdk_sr100_m4.overlay).
 * CONFIG_MC3419 and the standard SENSOR_CHAN_ACCEL_* channels were
 * already correct.
 *
 * Also confirmed on real hardware (2026-08-31): mc3419_init() leaves the
 * sensor in a low-power standby state on purpose -- per the driver's own
 * comment, it expects to be woken up either by setting a trigger or by
 * setting SENSOR_ATTR_SAMPLING_FREQUENCY (which internally writes the ODR
 * register and switches the device into "wake" mode). main() sets an ODR
 * via sensor_attr_set() once at startup to wake the sensor.
 *
 * A SECOND issue, found after the ODR fix alone still read back all-zero
 * X/Y/Z: mc3419_channel_get() converts the raw accel counts by
 * multiplying by an internal "sensitivity" field, and that field is only
 * ever populated inside mc3419_set_accel_range() -- which only runs when
 * SENSOR_ATTR_FULL_SCALE is explicitly set via sensor_attr_set().
 * mc3419_init() never calls it, and the driver's static data struct
 * starts zero-initialized, so sensitivity stays 0.0 and every reading
 * comes out as raw_value * 0 = 0, regardless of what the sensor
 * physically measures -- indistinguishable from "sensor asleep" purely
 * from the console. main() now also sets SENSOR_ATTR_FULL_SCALE (range
 * index 0, i.e. the first entry of the driver's internal sensitivity
 * table) once at startup, in addition to the ODR.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "ipc_common.h"

LOG_MODULE_REGISTER(lab09_client, CONFIG_LOG_DEFAULT_LEVEL);

#define SAMPLE_PERIOD_MS 500

static struct mbox_dt_spec tx_channel;
static const struct device *accel_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(mc3479));

static int32_t sensor_val_to_milli(struct sensor_value *v)
{
	return v->val1 * 1000 + v->val2 / 1000;
}

int main(void)
{
	struct ipc_accel_msg msg = {0};
	struct sensor_value ax, ay, az;

	LOG_INF("Lab09 Accel Telemetry CLIENT - %s", CONFIG_BOARD_TARGET);

	tx_channel = (struct mbox_dt_spec)MBOX_DT_SPEC_GET(DT_PATH(mbox_consumer), tx);

	if (accel_dev == NULL || !device_is_ready(accel_dev)) {
		LOG_ERR("accel device not ready (check DT_NODELABEL(mc3479))");
		return -ENODEV;
	}

	/* mc3419_init() leaves the sensor in low-power standby by design --
	 * setting an ODR (SENSOR_ATTR_SAMPLING_FREQUENCY) is what wakes it
	 * into active measurement mode. Without this, sample_fetch()
	 * "succeeds" but every channel reads back 0. */
	struct sensor_value odr = {.val1 = 50, .val2 = 0}; /* 50 Hz */

	if (sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ,
			     SENSOR_ATTR_SAMPLING_FREQUENCY, &odr) != 0) {
		LOG_ERR("sensor_attr_set(SAMPLING_FREQUENCY) failed -- accel may stay in standby");
	}

	/* Also required: the driver only fills in its internal sensitivity
	 * (used to convert raw counts to real units) when the full-scale
	 * range is explicitly set. Without this, every reading comes out
	 * as raw_value * 0 = 0 -- looks identical to "sensor asleep" from
	 * the console alone, but is actually a completely separate bug from
	 * the ODR/wake issue above. Range index 0 = the smallest (most
	 * sensitive) range in the driver's internal sensitivity table. */
	struct sensor_value range = {.val1 = 0, .val2 = 0};

	if (sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ,
			     SENSOR_ATTR_FULL_SCALE, &range) != 0) {
		LOG_ERR("sensor_attr_set(FULL_SCALE) failed -- readings may stay at 0");
	}

	while (1) {
		if (sensor_sample_fetch(accel_dev) == 0) {
			sensor_channel_get(accel_dev, SENSOR_CHAN_ACCEL_X, &ax);
			sensor_channel_get(accel_dev, SENSOR_CHAN_ACCEL_Y, &ay);
			sensor_channel_get(accel_dev, SENSOR_CHAN_ACCEL_Z, &az);

			msg.x = sensor_val_to_milli(&ax);
			msg.y = sensor_val_to_milli(&ay);
			msg.z = sensor_val_to_milli(&az);
			msg.seq++;

			struct mbox_msg mbox_msg = {.data = &msg, .size = sizeof(msg)};
			mbox_send_dt(&tx_channel, &mbox_msg);
		} else {
			LOG_WRN("sensor_sample_fetch failed");
		}

		k_msleep(SAMPLE_PERIOD_MS);
	}
	return 0;
}
