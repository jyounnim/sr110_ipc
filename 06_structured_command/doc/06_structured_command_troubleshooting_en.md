# Lab 06 Troubleshooting: Structured Command Message

This lab started with the M4-side `user_button` interrupt-support issue — already identified back in Lab 02/05 — **applied proactively as polling-mode from the outset**. As a result, no new issues came up during hardware verification.

### Symptom/Cause/Fix — (proactive fix, not an issue) M4 `user_button` interrupt not supported

**Symptom**: Not applicable — this did not occur in this lab. (The symptom as originally observed in Lab 02 was that the M4 boot log printed `interrupt configuration failed: -134` and button input was never detected at all.)

**Cause**: `gpio_exp0` (the PCAL6416A I2C GPIO expander) that `user_button` hangs off of has no `int-gpios` wired in the M4-side base devicetree. The `gpio_pcal64xxa.c` driver is written so that `pin_interrupt_configure()` always returns `-ENOTSUP` on an instance with no `int-gpios`, which means interrupt-based `gpio-keys` mode structurally cannot work on the M4 side.

**Fix**: The fix established in Lab 02/05 was applied here in advance, from the moment the code was written. `polling-mode` is set on the parent node (`&buttons`) in `lab/remote/boards/sr100_rdk_sr100_m4.overlay`, as shown below.

```dts
&buttons {
	polling-mode;
};
```

Thanks to this setting, Lab 06 passed hardware verification straight away with no button-related issues. `button_press_count` was confirmed to reflect correctly in the `GET_STATUS` reply, with a delay of one polling period (the default `debounce-interval-ms`, 30 ms).

### Reference: Common board issues that also apply to this lab

The two items below are not issues specific to this lab — they are fixes applied in common across every IPC lab starting with Lab 01. They're left here as a reference so a new problem isn't mistaken for one of these.

- **M4/M55 sharing the I2C1 bus**: because M4 and M55 physically share the same I2C1 bus, `lab/boards/sr100_rdk_sr100_m55.overlay` sets `&i2c1` (and its child nodes `&gpio_exp0`, `&ov02c10`) to `disabled` on the M55 side, so only M4 drives that bus. See the [Lab 01 troubleshooting doc](../../01_hello_ipc/doc/01_hello_ipc_troubleshooting_en.md) for the full root cause.
- **`M4_BUILD` relative path**: at M55 build time, `-DM4_BUILD="../m4"` is a path relative to the M55 build directory (`m55/`). `../m4` is correct when `m4/` and `m55/` are sibling directories under the workspace root; specifying `./m4` by mistake causes the M4 firmware to be silently omitted from the final image. See the [Lab 01 troubleshooting doc](../../01_hello_ipc/doc/01_hello_ipc_troubleshooting_en.md) for the full root cause.

If this document doesn't resolve your problem, please also check the related sections in the Lab 01/02 README documents linked above.
