# Lab 03: Full-Duplex Ping-Pong — Troubleshooting

This document records the concrete symptoms, root causes, and remediation history from validating Lab 03 on real hardware. For conceptual background (why the mbox callback must never block, etc.), see [`03_full_duplex_ping_pong_en.md`](./03_full_duplex_ping_pong_en.md). This document covers only the issues actually encountered and how they were reproduced and resolved.

## Symptoms / Root Cause / Fix

### Symptom: No log output at all on the M55 console (not even the boot banner)

M4 correctly received pings, toggled LED0, replied with pong, and kept waiting for the next ping — but the M55 console showed not even a boot banner (no log output whatsoever). M55 had sent the very first ping (confirmed, since M4 received seq=1 correctly) but from that point on it was completely unresponsive.

**Root cause**: M55's `mbox_rx_callback()` (the pong receive callback) was calling `k_msleep(PINGPONG_DELAY_MS)` directly. The mbox receive callback runs in ISR (interrupt) context, and blocking calls that put a thread to sleep — like `k_msleep()` — must never be used inside an ISR: doing so hangs the kernel on the spot.

Zephyr's default logging mode (deferred) buffers log messages and has a separate logging thread emit them later. Once the kernel hangs inside the ISR, that logging thread never gets a chance to be scheduled again, so every log already sitting in the buffer — including the boot banner — is lost and never reaches the screen. That's exactly why it looked, from the outside, as though M55 had done nothing at all.

M4 was written from the start using the correct pattern — enqueueing in the callback and doing the actual work (LED toggle + pong send) in a worker thread — so it kept running without any issue. That contrast (M4 fine, M55 hung) was the key clue that narrowed down the root cause.

**Fix**: Rewrote M55 to use the same ISR → `k_msgq` → worker thread pattern as M4. `mbox_rx_callback()` now only enqueues the pong; `k_msleep()` and sending the next ping are handled in a newly added `Pong_Task` worker thread.

**Re-verification result (2026-08-31)**: Re-verifying the fixed build confirmed that the M55 boot banner now prints normally, PING/PONG seq numbers keep incrementing, and M4's LED0 toggles on a steady cycle as expected. This issue is fully resolved.

## Note: If Your Lab Uses gpio-keys Buttons (Not Applicable to This Lab)

This lab doesn't use a button, so the issue below doesn't apply here — but in later labs that use the button (`user_button`) (05, 15, 17, 18, and others), the issue found in Lab 02 can resurface: on this board, M4's `gpio_exp0` (PCAL6416A) has no `int-gpios` wired up, so interrupt-based `gpio-keys` always fails with `-ENOTSUP` (-134). The M4 overlay needs the following added (it must be attached to the **parent** `gpio-keys` node — attaching it to the child `user_button` node instead causes a devicetree binding error).

```dts
&buttons {
	polling-mode;
};
```

See the [Lab 02 troubleshooting doc](../../02_button_pong/doc/02_button_pong_troubleshooting_en.md) for the full root-cause explanation.
