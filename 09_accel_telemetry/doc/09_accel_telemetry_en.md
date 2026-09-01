# Lab 09: Accelerometer Telemetry (M4 → M55)

This is the curriculum's first "sensor telemetry" lab: the onboard MC3419 accelerometer is sampled periodically by the M4 and streamed to the M55.

## Learning Objectives

- Learn to drive a real hardware sensor with Zephyr's **standard sensor subsystem API** (`sensor_sample_fetch()` / `sensor_channel_get()` / `sensor_attr_set()`).
- Understand the convention of re-enabling a devicetree node in an overlay when it ships `disabled` by default.
- Implement a **periodic telemetry push** pattern in the M4→M55 direction, as distinct from event-driven IPC (button interrupts and the like).

## Connection to Previous Labs

Labs 01–08 all exchanged **digital, discrete events** over IPC: something "happened" (a button was pressed, a command arrived) and a single message went out in response.

Lab 09 is different in character. An accelerometer is an **analog (continuous) sensor** that always has a value, and instead of waiting for a specific event, this lab **reads and sends a value unconditionally on a fixed cadence (500 ms)**. This is called the "telemetry push" pattern — the sender (M4) pushes data on its own schedule, whether or not the receiver (M55) has asked for it.

> This pattern is contrasted directly with an "event-driven" one in the very next lab, Lab 10. Lab 10 uses the same sensor but only sends a notification when a value crosses a threshold — it's worth understanding the trade-off between the two patterns (simplicity vs. bandwidth/power efficiency).

## Core IPC Concepts

### 1. Periodic Telemetry Push vs. Event-Driven

| | This lab (Lab 09) | Lab 10 (preview) |
|---|---|---|
| When it sends | Always, on a fixed cadence (500 ms) | Only when a value crosses a threshold |
| Advantage | Simple to implement; the receiver never misses the latest state | Minimizes IPC traffic / power consumption |
| Disadvantage | Keeps sending even when the value hasn't changed (wastes bandwidth) | Needs extra logic to decide "has the value changed" |

Both patterns are common in practice; which one to pick depends on whether the receiver needs to always know the latest value, or only needs to know when something changes.

### 2. Using the Zephyr Sensor Subsystem

Zephyr's standard sensor API is built around three functions:

- `sensor_sample_fetch(dev)` — reads the latest raw measurement from the sensor into the driver's internal buffer.
- `sensor_channel_get(dev, chan, &val)` — pulls the value of a specific channel (X axis, Y axis, …) out of the just-fetched sample.
- `sensor_attr_set(dev, chan, attr, &val)` — configures an operating attribute of the sensor (sampling frequency, measurement range, etc.).

That much is well-known usage, but getting a sensor to actually work "properly" requires three things to be in place at init time. These aren't specific to the MC3419 — remember them as a **general checklist for working with Zephyr sensor drivers**, because they apply directly to later labs (10, 12, 13, 15, 18) as well.

**① Confirm the devicetree node is enabled (`status = "okay"`).**
Many boards' base devicetrees define onboard peripheral nodes with a default `status = "disabled"`. This is a common convention to avoid unnecessarily compiling/initializing a driver for a peripheral an application doesn't use, or conflicting with another peripheral over a pin/bus. To actually use the sensor, the application (via an overlay) must explicitly turn it back on.

```dts
&mc3479 {
	status = "okay";
};
```

If the node isn't enabled, `DEVICE_DT_GET_OR_NULL()` returns `NULL`, and the `device_is_ready()` check fails with "device not ready".

**② Set the ODR (output data rate) with `SENSOR_ATTR_SAMPLING_FREQUENCY` to wake the sensor.**
Many low-power sensors are designed to stay in standby even after power-up, rather than starting measurement immediately. Calling `sensor_attr_set()` to set the ODR (Output Data Rate) is what triggers the transition into active measurement mode.

```c
struct sensor_value odr = {.val1 = 50, .val2 = 0}; /* 50 Hz */
sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ,
		 SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
```

**③ Set the measurement range with `SENSOR_ATTR_FULL_SCALE` to activate the scale (sensitivity).**
A sensor driver typically keeps an internal scale factor used to convert the raw integer value read from a register into a real physical unit (e.g., acceleration in mg). This factor is often not populated automatically at init time — it's computed and filled in only when the application explicitly sets the desired measurement range via `SENSOR_ATTR_FULL_SCALE`.

```c
struct sensor_value range = {.val1 = 0, .val2 = 0}; /* most sensitive (smallest) range */
sensor_attr_set(accel_dev, SENSOR_CHAN_ACCEL_XYZ,
		 SENSOR_ATTR_FULL_SCALE, &range);
```

Skipping either ② or ③ produces the exact same visible symptom — "always 0" — whether the sensor is asleep or the multiplier applied to the raw value is zero, so the surest way to tell which one is the cause is to check the driver source directly.

## Architecture

```
M4 (CLIENT)                         M55 (HOST)
─────────────                       ─────────────
enable mc3419 node (overlay)
device_is_ready()
sensor_attr_set(ODR)      ─┐
sensor_attr_set(FULL_SCALE)│  init (once)
                            ┘
loop (every 500ms):
  sensor_sample_fetch()
  sensor_channel_get() x3 (X/Y/Z)
  msg = {x, y, z, seq++}
  mbox_send_dt(&tx_channel, &msg) ──mbox──▶ rx_cb(ISR)
                                             LOG_INF(x, y, z, seq)
```

- **M4 (`lab/remote/src/main.c`, CLIENT)**: opens the `mc3479` node, sets the ODR and measurement range once at boot, then every 500 ms reads X/Y/Z, packs them into a `struct ipc_accel_msg`, and sends it with `mbox_send_dt()`.
- **M55 (`lab/src/main.c`, HOST)**: `rx_cb()` logs the message with `LOG_INF()` as soon as it arrives, and returns. Because this lab is M4→M55 unidirectional, the M55 never needs to reply to the M4, and since logging itself isn't a blocking call, it's safe to handle everything directly inside the ISR callback without a separate message queue or worker thread.

The message struct (`lab/include/ipc_common.h`, `lab/remote/include/ipc_common.h`) carries X/Y/Z plus a sequence number:

```c
struct ipc_accel_msg {
	int32_t x;
	int32_t y;
	int32_t z;
	uint32_t seq;
};
```

> **Design note — never block inside an mbox callback**: the M55's `rx_cb()` only logs and returns; it never calls `mbox_send_dt()` from inside the callback either (there's nothing for the M55 to send back to the M4 in this unidirectional lab). This principle was established in Labs 03/07 and applies across the whole curriculum.

## Devicetree Configuration

The M4 and M55 physically share the same I2C1 bus. Throughout this curriculum, only the M4 owns this bus — the M55 overlay disables `&i2c1` (see the Lab 01 doc for the background). The accelerometer-related configuration is only needed in the M4 overlay (`lab/remote/boards/sr100_rdk_sr100_m4.overlay`).

```dts
/* ipc0 shared-memory-size must match the M55 overlay exactly. */
&ipc0 {
	shared-memory-size = <0x400>;
};

/* The onboard MC3419 accelerometer defaults to status = "disabled" in the base dts.
 * The M4 owns I2C1, so it's explicitly re-enabled here. */
&mc3479 {
	status = "okay";
};
```

The `mc3479` node's `int-gpios` is only used when the driver's interrupt-based trigger mode (`CONFIG_MC3419_TRIGGER_OWN_THREAD`) is enabled. This lab only polls via `sensor_sample_fetch()` (`CONFIG_MC3419_TRIGGER` defaults to `none`), so interrupt wiring isn't a concern here.

`lab/remote/prj.conf` needs the following settings:

```
CONFIG_MBOX=y
CONFIG_I2C=y
CONFIG_SENSOR=y
CONFIG_MC3419=y
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_PRINTK=y
```

## How to Build

```bash
# 1) Build M4 (remote) first
west build -p always -b sr100_rdk/sr100/m4 ./09_accel_telemetry/lab/remote -d m4

# 2) Build M55 (host, embedding the M4 binary via M4_BUILD)
west build -p always -b sr100_rdk/sr100/m55 ./09_accel_telemetry/lab -d m55 \
    -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD="../m4"
```

> `M4_BUILD` is a relative path from the M55 build directory (`m55/`). If `m4/` and `m55/` are created as sibling directories under the workspace root, `../m4` is correct.

## Running It and Checking the Result

After flashing the M55 image and resetting the board, the M55 releases the M4 from reset at boot and gets ready to receive over mbox. The M4 initializes the accelerometer immediately at boot and starts sending values every 500 ms.

The M55 console keeps printing X/Y/Z values with an incrementing sequence number:

```
[00:00:00.512,000] <inf> lab09_host: accel seq=1 x=... y=... z=...
[00:00:01.012,000] <inf> lab09_host: accel seq=2 x=... y=... z=...
[00:00:01.512,000] <inf> lab09_host: accel seq=3 x=... y=... z=...
```

Move or tilt the board and you'll see the values change.

### Note — Uncalibrated Raw Values

The X/Y/Z values coming out of this lab are raw values with no factory calibration applied. Because gravity (~1g) is always loaded onto one axis or another even when the board is perfectly still, it's normal for the at-rest values to not be exactly zero. This curriculum is about learning IPC patterns, not building a precision instrument, so calibration and measurement accuracy are out of scope.

## Summary

- An always-on analog sensor like an accelerometer is naturally handled with the "periodic telemetry push" pattern.
- Using a Zephyr sensor properly takes more than just `sensor_sample_fetch()`/`sensor_channel_get()` — at init time you also need to enable the devicetree node, set the ODR (wake), and set the measurement range (scale).
- Unidirectional telemetry can be implemented safely without a message queue or worker thread, since the receiving callback only needs to log.

## Coming Up Next

Lab 10 uses the same MC3419 sensor, but switches from "send every time" to an **event-driven notification** pattern that only alerts when a value crosses a threshold. Comparing the two IPC patterns on the same sensor is the key learning point connecting this lab and Lab 10.

---

Ran into a problem? → see [`09_accel_telemetry_troubleshooting_en.md`](./09_accel_telemetry_troubleshooting_en.md)
