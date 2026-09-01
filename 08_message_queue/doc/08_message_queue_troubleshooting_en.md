# Lab 08 Troubleshooting: Multi-Type Message Queue

This lab reflects the principle established in Lab 07 (Echo Service) — **"the mbox callback must never block, no exceptions, not even `mbox_send_dt()`"** — from the design stage onward, and it passed hardware verification with no notable issues. Below is the background for that, along with things to check should you run into similar symptoms.

## Why this lab was safe from the start

In Lab 07, an assumption existed that lightweight APIs were fine to call directly from inside the mbox callback (ISR); on-hardware verification confirmed that `mbox_send_dt()` itself could block on this board, and that exception was completely retired as a result. From that point on, every lab has had to follow the principle "make no IPC call whatsoever inside the callback other than enqueuing."

Lab 08's M4-side `mbox_rx_callback()` matches this principle exactly.

```c
static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
                              void *user_data, struct mbox_msg *data)
{
    struct ipc_pattern_msg msg;

    if (data->size < sizeof(msg)) {
        return;
    }
    memcpy(&msg, data->data, sizeof(msg));
    k_msgq_put(&pattern_msgq, &msg, K_NO_WAIT);
}
```

- This callback **never calls `mbox_send_dt()`.** This lab is designed from the outset as a one-way M55→M4 structure with no M4→M55 response, so the scenario that caused problems in Lab 07 — "the callback calls `mbox_send_dt()` and it blocks" — simply doesn't apply here.
- Everything that actually takes time (running the LED pattern, including several `k_msleep` calls) happens exclusively in a separate `Pattern_Task` worker thread.
- `k_msgq_put(..., K_NO_WAIT)` returns failure immediately, with no wait, even when the queue is full — so there's no way for the ISR to block on the enqueue operation itself either.

In other words, this lab didn't just "happen to have no problems" — it was **designed so that the subject of queuing itself naturally aligns with the no-blocking-in-callback principle,** which is why it passed verification with no changes needed.

## No known issues

As of this writing (v1.0.0), no Lab-08-specific hardware or build issues have been reported. Only the items below, which are shared across every lab, apply.

- **I2C1 bus-sharing issue**: Because the M4 and M55 physically share the same I2C1 bus on this board, `&i2c1`, `&gpio_exp0`, and `&ov02c10` must be turned off with `status = "disabled"` in the M55 overlay. This lab's M55 overlay (`lab/boards/sr100_rdk_sr100_m55.overlay`) already reflects this. See the [Lab 01 troubleshooting document](../../01_hello_ipc/doc/01_hello_ipc_troubleshooting_en.md) for the underlying cause.
- **`M4_BUILD` relative path**: When building the M55, `-DM4_BUILD="../m4"` is a relative path with respect to the M55 build directory (`m55/`). If `m4/` and `m55/` are not sibling directories under the workspace root, adjust this value to match your actual layout.

## If queuing doesn't behave as expected

This lab itself passed on hardware with no issues, but the notes below are kept for reference in case you run into these symptoms while modifying the code for your own experiments.

- **`queue depth now` always prints 0**: This happens when the M55's four-message burst timing happens to line up with the M4's processing speed such that nothing ever gets a chance to pile up. Check whether there's any delay between the `send_pattern()` calls, and whether the `repeat` values are what you intended.
- **Some messages appear to have vanished**: This happens when more messages arrive in a single burst than the queue depth (the third argument to `K_MSGQ_DEFINE`, currently 8) can hold, causing `k_msgq_put()` to fail with `-ENOMSG`. This lab's callback doesn't check that return value, so for root-causing this in your own experiments, it's worth adding a return-value check and a drop counter to the callback.
- **LEDs behave differently from the intended pattern**: Check that the `switch (msg->pattern_id)` branches in `run_pattern()` line up exactly with `enum ipc_pattern_id` (1–4), and that the `ipc_common.h` on the M55 and M4 sides are the same version.

---

If you discover a new issue, please add it to this document in symptom / cause / resolution order.

