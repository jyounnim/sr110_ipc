# Lab 04: Shared Counter — Troubleshooting

This lab passed hardware validation with no notable issues.

Code review confirmed that M4's `mbox_rx_callback()` was written correctly from the start: it only pushes the received data onto the queue, while the actual processing (the blocking `k_msleep()` calls needed for LED blinking) is handled on the `Blink_Task` worker thread — so the "blocking call inside an ISR callback" bug found in Lab 03 never had a chance to occur here. The M55 side is a unidirectional (M55 → M4) lab with no mbox receive callback at all, so it isn't applicable there.

On-device testing immediately confirmed correct behavior end to end: `counter=N sent` on M55, `counter=N received -> blinking LED0 M times` on M4, and the actual LED0 blinking.

## Reference: Common Issues Already Resolved in an Earlier Lab, and Applicable Here

Two issues apply in common to every lab, including Lab 04, and were first discovered and fixed back in Lab 01. They did not recur specifically in Lab 04, but are noted here for reference.

- **M4/M55 I2C1 bus sharing conflict**: M4 and M55 physically share the same I2C1 bus (the `gpio_exp0` expander that LED0/LED1 and the button hang off of), so building both cores together causes a bus initialization conflict at boot. `lab/boards/sr100_rdk_sr100_m55.overlay` sets `&i2c1`, `&gpio_exp0`, and `&ov02c10` to `disabled` so that M4 alone owns this bus. See the [Lab 01 documentation](../../01_hello_ipc/doc/01_hello_ipc_en.md) for the full root cause.
- **`M4_BUILD` relative path**: `M4_BUILD` is resolved as a relative path against the M55 build directory (`m55/`). If `m4/` and `m55/` are sibling directories at the workspace root, `-DM4_BUILD="../m4"` is the correct value; passing the wrong value silently (with no error) omits the M4 firmware from the final flash image, leaving M4 running nothing at all.
