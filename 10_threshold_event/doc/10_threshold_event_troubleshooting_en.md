# Lab 10 Troubleshooting: Threshold Event Notification

This document collects issues actually encountered — and how they were resolved — while validating Lab 10 on real hardware (the sr100_rdk board). Issues around the node label (`mc3479`), re-enabling status, ODR wake, and `SENSOR_ATTR_FULL_SCALE`/sensitivity were already identified and addressed in Lab 09, so they are not repeated here (see [Lab 09 Troubleshooting](../../09_accel_telemetry/doc/09_accel_telemetry_troubleshooting_en.md) instead).

---

## Issue 1 — Events Kept Firing Even at Rest

### Symptom

On the M55 console, `THRESHOLD EVENT` kept firing continuously starting right after boot, even with the board sitting perfectly still.

```
[00:00:10.624,000] <wrn> lab10_host: THRESHOLD EVENT seq=21 x=358 y=-236 z=-10186
[00:00:11.128,000] <wrn> lab10_host: THRESHOLD EVENT seq=22 x=193 y=0 z=-10092
...
```

### Cause

As the z-axis value in the log consistently sitting around -10000 suggests, gravitational acceleration (~1g) was always present in the raw values, even at rest. The code at the time compared magnitude (the raw value's absolute strength) against the threshold relative to the origin (0,0,0), so no matter what orientation the board was in, the gravity component alone already exceeded the threshold, making every reading register as "exceeded" even while the board sat still. In short, the code was measuring "how large is the absolute strength including gravity," not "how much did it actually move."

### Fix

After confirming with the user ("this calibration should happen on M4, right?"), calibration was designed to run **on M4**, since M4 is the side that physically drives the sensor:

- Collect samples for about 1 second right after boot (`CALIB_SAMPLES` = 10 × `POLL_PERIOD_MS` = 100 ms) and store the X/Y/Z averages as the baseline.
- Change every subsequent sample to compare its deviation from this baseline (`dx = x - base_x`, `dy = y - base_y`, `dz = z - base_z`) against the threshold, instead of the raw value.

This absorbs and cancels out the gravity offset and any fixed tilt present at boot into the baseline, so only genuine "movement" is caught as an event.

**Note**: This baseline is a fixed snapshot taken once at boot, not a continuously updating adaptive filter. If the board is moved during calibration (roughly the first second after boot), that motion gets mixed straight into the baseline and skews every judgment made afterward.

---

## Issue 2 — Still Exceeding the Threshold from Noise After Calibration; Threshold Raised

### Symptom

Even after applying the calibration from Issue 1, events kept firing continuously while the board sat still. M4 console log:

```
[00:00:12.017,000] <inf> lab10_client: [Threshold_Task] event sent seq=8 mag=1201 (dev x=73 y=35 z=-1093)
[00:00:12.723,000] <inf> lab10_client: [Threshold_Task] event sent seq=9 mag=1515 (dev x=-328 y=-29 z=-1158)
```

`dev z` (the z-axis deviation from baseline) kept sitting around -1000 to -1190 even at rest, and once the small x/y noise was added on top, `mag` repeatedly exceeded the threshold of 1200 that was in effect at the time.

### Cause

Calibration canceled out the fixed offset at boot time (the gravity component plus initial tilt), but this particular board/sensor unit still had **residual noise** — the width by which the baseline itself wanders — that measured as high as `mag≈1515` in practice. The originally configured threshold of 1200 was smaller than even the peak of this residual noise, so it was bound to be exceeded at rest no matter what.

### Fix

At the user's request, the threshold was raised to **2000 milli-g**:

- M55: `ACCEL_THRESHOLD_MILLI_G` in `lab/src/main.c`
- M4: the default value of `threshold_milli_g` in `lab/remote/src/main.c`

(Since M55 re-sets this on M4 at boot via the `IPC10_CMD_SET_THRESHOLD` command in both cases, the M55-side value is what ultimately takes effect. The M4-side default was still updated to match, for consistency.)

2000 leaves comfortable headroom above the measured peak noise (mag≈1515). That said, this value may need to be retuned again depending on hardware unit-to-unit variation or installation conditions (how the board is mounted, proximity to vibration sources, etc.). It's recommended to observe the `mag` values printed on the M4 console at rest a few times and set the threshold to a value clearly above their maximum.

---

## Design Note — mbox Callbacks Must Never Block

Per the principle established in Lab 03/Lab 07, both `rx_cb()` implementations, on M55 and M4, avoid heavy processing and return immediately — M4 only enqueues the command (`k_msgq_put(..., K_NO_WAIT)`), and M55 only logs the event's contents. All of M4's actual work (accelerometer polling/calibration, threshold updates, and event transmission including the `mbox_send_dt()` call) happens entirely in the `Threshold_Task` worker thread, so this lab never violated this principle to begin with.
