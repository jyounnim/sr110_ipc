# Lab 02: Button Pong — Troubleshooting Notes

This document records the problems actually encountered during Lab 02's development and hardware validation, along with how they were resolved. All fixes are already reflected in the current final code; this document exists purely as a reference record.

## Issue 1: gpio_exp0 Does Not Support Interrupts (2026-08-31)

### Symptom

The following errors appeared in the M4 boot log, and pressing the button produced no response at all.

```
[00:00:00.010,000] <err> gpio_keys: interrupt configuration failed: -134
[00:00:00.010,000] <err> gpio_keys: Pin 0 interrupt configuration failed: -134
```

`-134` is Zephyr's `-ENOTSUP`.

### Cause

`user_button` sits behind `gpio_exp0` (PCAL6416A, an I2C GPIO expander), and **the `gpio_exp0` definition in the M4-side base dts has no `int-gpios` property** (the M55-side definition does have `int-gpios = <&gpioa 3 GPIO_ACTIVE_LOW>;`, but it's moot since the M55 disables i2c1). The `gpio_pcal64xxa.c` driver is written so that `pin_interrupt_configure()` always returns `-ENOTSUP` for any instance lacking `int-gpios`, which means the default interrupt-based `gpio-keys` mode simply cannot work on this board's M4 side at all.

### Fix

`zephyr/dts/bindings/input/gpio-keys.yaml` provides a `polling-mode` boolean property for exactly this situation, so the following was added to the M4 overlay (`remote/boards/sr100_rdk_sr100_m4.overlay`) to switch from interrupts to periodic polling (at the default `debounce-interval-ms` of 30 ms):

```dts
&buttons {
	polling-mode;
};
```

## Verification History (Confirmed Items)

The following three items were "unverified" during development and have since all been confirmed through hardware testing.

1. **The `led1` node**: The board's original dts had no `led1` alias under `aliases`, so `DT_NODELABEL(led1)` was used initially. Afterward, `led1 = &led1;` was added directly to the `aliases` node in both the M4 and M55 dts, so `led1` is now accessed the same way as `led0`, via `DT_ALIAS(led1)` (landed 2026-08-30).
2. **Whether `user_button` is exposed through Zephyr's Input subsystem (`CONFIG_INPUT_GPIO_KEYS`)**: confirmed to be exposed correctly. However, it must operate in polling mode rather than interrupt mode (see Issue 1 above).
3. **Whether gpio_exp0 (PCA6416A) can raise button events via interrupts**: (2026-08-31) confirmed that the M4 side cannot use interrupts because `int-gpios` is not wired, resolved via `polling-mode`.

## Reference: M4/M55 I2C1 Bus-Sharing Issue

The `i2c1` disable and `gpio_exp0`/`ov02c10` disable settings in Lab 02's M4/M55 overlays are not a new issue discovered in Lab 02 — they were found during Lab 01 hardware testing and have since been applied uniformly across every lab. See Lab 01's troubleshooting document for the detailed root-cause analysis.
