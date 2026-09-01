# Lab 09 Troubleshooting: Accelerometer Telemetry

This document walks through three real issues that were found, in sequence, while validating Lab 09 on an actual SR110 board (`sr100_rdk`), along with the diagnostic process for each. The three mandatory steps the main doc ([`09_accel_telemetry_en.md`](./09_accel_telemetry_en.md)) describes as "normal usage" — re-enabling the node, the ODR wake-up, and the FULL_SCALE setting — were, in fact, each discovered by hitting one of the bugs below.

## Issue 1 — accel device not ready

### Symptom

The M4 console shows the following error, and `main()` exits early with `-ENODEV`.

```
[00:00:00.007,000] <inf> lab09_client: Lab09 Accel Telemetry CLIENT - sr100_rdk/sr100/m4
[00:00:00.007,000] <err> lab09_client: accel device not ready (check DT_NODELABEL(accel0))
```

### Root-Cause Diagnosis

The original code was written assuming the accelerometer's devicetree node label was `accel0` (it had been confirmed that Zephyr upstream has an `mc3419` driver, and that the `CONFIG_MC3419` / `SENSOR_CHAN_ACCEL_X/Y/Z` APIs exist, but the actual node label in the board's dts had not been checked before the code was handed off).

After reproducing the error above on real hardware, the actual board dts files (`sr100_rdk_m4.dts` / `sr100_rdk_m55.dts`) were opened directly, revealing two things that differed from the original assumption.

```dts
&i2c1 {
	...
	mc3479: mc3419@4c {
		compatible = "memsic,mc3419";
		status = "disabled";
		reg = <0x4c>;
		int-gpios = <&gpio_exp0 4 0>;
		lpf-fc-sel = <0>;
	};
};
```

1. The node label was **`mc3479`**, not `accel0`.
2. This node defaults to **`status = "disabled"`** in both the M4 and M55 base dts, and the code to re-enable it in an overlay had simply been omitted. `DEVICE_DT_GET_OR_NULL()` returns `NULL` for a disabled node, so there was no device pointer at all before the `device_is_ready()` check even ran — which is exactly what surfaced as the "not ready" error.

The `CONFIG_MC3419` Kconfig symbol and the standard `SENSOR_CHAN_ACCEL_X/Y/Z` sensor API channels turned out to be correct in the original assumption (reconfirmed against the Zephyr upstream `drivers/sensor/memsic/mc3419/` driver).

### Fix

- Added `&mc3479 { status = "okay"; };` to `lab/remote/boards/sr100_rdk_sr100_m4.overlay` to explicitly enable the node on the M4 side.
- Changed `DT_NODELABEL(accel0)` → `DT_NODELABEL(mc3479)` in `lab/remote/src/main.c`.
- Note: the `mc3479` node's `int-gpios` points at pin 4 of `gpio_exp0`, but that's only used when the driver's interrupt-based trigger mode (`CONFIG_MC3419_TRIGGER_OWN_THREAD`) is enabled. This lab only polls via `sensor_sample_fetch()` (trigger mode unused, `CONFIG_MC3419_TRIGGER` defaults to `none`), so this is unrelated to the "M4-side `gpio_exp0` has no `int-gpios`, so interrupts aren't supported" issue found in Labs 02/05/06.
- The M55-side base dts also has `mc3479` disabled by default the same way, so the M55 overlay needs no changes at all (leaving it disabled as-is is correct).

## Issue 2 — x=y=z=0 stuck, no change when moving the board

### Symptom

After fixing Issue 1, the IPC itself works fine (seq keeps incrementing), but the `x/y/z` values arriving at the M55 stay stuck at 0. Moving the board produces no change.

### Root-Cause Diagnosis

The Zephyr MC3419 driver (`mc3419_init()`) is **deliberately designed to stay in a low-power standby state right after init**. The driver source has a comment that says exactly this:

> "Leave the sensor in default power on state, will be enabled by configure attr or setting trigger"

In other words, the driver is designed so that setting the ODR (Output Data Rate) via `sensor_attr_set(SENSOR_ATTR_SAMPLING_FREQUENCY)` is what triggers the register write that switches the sensor into wake mode. That call was missing from the code, so `sensor_sample_fetch()` "succeeded" without error, but it was actually reading registers that had never started measuring — all zeros.

### Fix

Right after the `device_is_ready()` check in `main()`, added a `sensor_attr_set()` call to set the ODR to 50 Hz and wake the sensor.

```c
struct sensor_value odr = {.val1 = 50, .val2 = 0}; /* 50 Hz */
sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
```

A log line (`sensor_attr_set(SAMPLING_FREQUENCY) failed`) was added to report a failure of this call.

## Issue 3 — x=y=z=0 persists even after setting the ODR (a second, same-day root cause)

### Symptom

Even after applying the Issue 2 ODR fix, the M4 initialized cleanly with no error logs at all (no `sensor_attr_set` failures, nothing), yet the x/y/z values arriving at the M55 were still stuck at 0. From the console alone, this was indistinguishable from "the sensor is still asleep."

### Root-Cause Diagnosis

The console logs alone weren't enough to narrow this down, so the Zephyr MC3419 driver source (`drivers/sensor/memsic/mc3419/mc3419.c`) was checked directly.

```c
static int mc3419_to_sensor_value(double sensitivity, int16_t *raw_data,
                                  struct sensor_value *val)
{
        double value = sys_le16_to_cpu(*raw_data);
        value *= sensitivity * SENSOR_GRAVITY_DOUBLE / 1000;
        return sensor_value_from_double(val, value);
}
```

The `sensitivity` multiplier applied to the raw value is **only assigned inside `mc3419_set_accel_range()`**, and that function only runs when `sensor_attr_set(..., SENSOR_ATTR_FULL_SCALE, ...)` is explicitly called. `mc3419_init()` never calls it, and the driver's static data struct is zero-initialized, so `sensitivity` stayed at 0 — meaning `raw_value * 0 = 0` regardless of the actual measurement.

This was a **completely separate bug** from Issue 2 (the ODR/wake problem), but since the visible console symptom (always 0) was identical, the two were found one at a time in sequence.

### Fix

Added a `sensor_attr_set()` call for the full-scale range right after the ODR setting in `main()`.

```c
struct sensor_value range = {.val1 = 0, .val2 = 0}; /* smallest/most sensitive range */
sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &range);
```

In short, actually using this sensor requires setting **both the ODR and the range** explicitly via `sensor_attr_set()` — missing either one produces the exact same visible symptom (value stuck at 0), which makes them hard to tell apart from logs alone.

> **Lesson for every future sensor lab**: in later labs that use sensors (10/12/13/15/18), if the device is ready and IPC is working but the value is stuck at 0 (or otherwise fixed), don't assume it's just wake-up (ODR) — check the actual driver source for other required `attr_set` calls the driver expects (range/scale, etc. — there can be more than one). This is sometimes a missing runtime API call, not a devicetree/Kconfig problem.

## Decision: Calibration/Precision Is Out of Scope

After all three issues above were fixed and the lab re-validated, IPC and the sensor reading itself worked correctly — but even at rest right after boot, the values weren't exactly zero; one axis always carried a value corresponding to gravity (~1g).

This isn't a driver bug — it's simply because the raw values are being used as-is, without factory calibration (offset/scale correction) applied. Since this curriculum's goal is to teach M4↔M55 IPC patterns, not to build a precision accelerometer, calibration and measurement-accuracy improvements were explicitly decided to be out of scope for this lab, and the lab was completed as-is using raw values.

> For reference, this "there's always a value present even at rest" characteristic comes back from a different angle in Lab 10. Lab 10 uses the same sensor to build threshold-based events, and if the threshold is compared against a raw zero baseline, the gravity offset causes events to keep firing even while the board is at rest. That's solved (not through calibration, but) by storing the value at boot as a baseline and comparing only the deviation from it — the goal there is "prevent false-positive events," not "make the measurement accurate," which is a separate concern from the calibration discussion in this lab.
