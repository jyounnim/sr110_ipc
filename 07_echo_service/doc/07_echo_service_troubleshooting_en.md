# Lab 07 Troubleshooting: Hang from Calling mbox_send_dt() Directly Inside the M4 mbox Callback (2026-08-31)

This document records, in as much detail as possible, the symptoms, diagnostic process, root cause, and fix for a serious hang bug actually encountered on real hardware during the first implementation of Lab 07. This is the single most educationally valuable case study in this entire curriculum, so it is laid out here in full, in the actual order events unfolded — not summarized away.

Before reading this, we recommend first reading the "Why even `mbox_send_dt()` must not be called from inside a callback" section of the main document ([07_echo_service_en.md](07_echo_service_en.md)) for background. This document covers the actual debugging journey that led to that conclusion.

## Background — The Initial Implementation

In the first version of Lab 07, the M4 (remote) side's mbox rx callback was written as follows (reconstructed here):

```c
static void mbox_rx_callback(const struct device *dev, mbox_channel_id_t channel_id,
                              void *user_data, struct mbox_msg *data)
{
    struct mbox_msg reply;
    static uint8_t buf[ECHO_MAX_LEN];
    size_t len = MIN(data->size, sizeof(buf));

    memcpy(buf, data->data, len);

    /* "It's just a lightweight register write, so calling it directly
     * from the callback should be fine." */
    reply.data = buf;
    reply.size = len;
    mbox_send_dt(&tx_channel, &reply);
}
```

The reasoning at the time was: "This lab has no obviously blocking operation like an I2C transaction, so calling `mbox_send_dt()` directly from the callback (ISR) should be safe." By Labs 01–03, the principle "avoid explicit blocking calls like `k_msleep()` and time-consuming hardware access like I2C inside an ISR" was already well established — but `mbox_send_dt()` itself was not recognized as one of those obvious blocking points.

## Symptoms

Booting M55 and M4 together on real hardware (the sr100_rdk board) and observing the result:

- The M55 console printed `Sending: "..."` **exactly 3 times** (at t=0s, t=2s, t=4s), and then stopped producing any log output at all, permanently.
- The M4 console printed its normal boot logs (`Lab07 Echo Service CLIENT - ...`, `Echo service ready.`), but from that point on, **not a single line** related to message reception ever appeared.
- To narrow the problem down, a diagnostic log (`rx N bytes, echoing back`-style) was added right at the entry point of the M4 callback — but **that diagnostic log never appeared in the console either.** In other words, log output alone couldn't even confirm whether the callback had been invoked at all.
- The bug was 100% reproducible — rebooting, or waiting well over a minute, always produced the exact same hang at the exact same point (right after M55's 3rd send).

At this point, the observations alone made it hard to tell whether "M4 had hung," "M55 had hung," or "the mbox channel itself had died." Combining the point at which the M55 log stopped (after the 3rd send) with the fact that no diagnostic log ever appeared on M4 gave a strong suspicion that something on the M4 side had silently frozen — but that immediately raised a separate question of its own: "why doesn't even the log show up?"

## Diagnostic Process

### Step 1 — The absence of logs is itself a clue

The first thing to recognize was that "the diagnostic log I added to the code never shows up" doesn't necessarily mean "execution simply never reached that point." Zephyr's logging subsystem, by default, uses a **deferred logging** architecture: log messages are enqueued, and a separate logging thread drains that queue and actually writes the output to UART. In other words, even if the `LOG_INF()` call itself succeeded, if the logging thread responsible for flushing that content to the console never gets scheduled, the log will never appear on the console — ever.

This was exactly the same pattern already experienced once before, in Lab 03: if blocking occurs inside an ISR, that core's entire scheduler halts, and no thread — including the logging thread — can run anymore. The very fact that "we can't even tell from the logs whether the code reached that point" was, ironically, itself strong evidence supporting the hypothesis that "something inside an ISR is blocking."

### Step 2 — Identifying the only suspect inside the callback

The only code that executes inside M4's mbox rx callback was `memcpy()` and `mbox_send_dt()` — nothing else. `memcpy()` is a pure memory copy that clearly cannot block, so ruling it out left only one suspect: `mbox_send_dt()`.

At this point, "`mbox_send_dt()` can block" was still an unverified hypothesis — Zephyr's official API documentation alone gave no clear indication of whether this function could block in a particular board's backend implementation, and the conventional assumption of "a lightweight, register-write-level function" was also working against the hypothesis.

### Step 3 — Checking whether the M55-side symptoms fit the hypothesis

Rather than trying to verify the hypothesis directly, we went back and checked how well the already-observed symptoms fit it. The key detail was that "M55 succeeded in sending exactly 3 times, then stopped."

- M55's `mbox_send_dt()` call runs in the ordinary thread context of `main()`, so even if that call itself blocks, it doesn't freeze the M55 core (blocking in thread context is a normal, schedulable event).
- However, an mbox channel can have a finite number of internal buffering slots, and if the peer core (M4) never drains them, sends will start blocking, waiting for a free slot, once those slots run out.
- If M4 had **already frozen inside its callback while trying to process the very first message**, then M4 would never free up any slots after that point. In that state, M55 succeeding at a few sends before eventually running out of slots and stalling matches the observation — "exactly as many sends succeed as there are free channel slots, and then it stops" — precisely.

This consistency check gave real weight to the hypothesis that M4 had, in fact, frozen while processing the first message's callback — specifically, at the point of the `mbox_send_dt()` call.

## Root Cause

**On this board's mbox backend, `mbox_send_dt()` itself can block internally** — confirmed, for example, to occur when a new send is attempted before a previously sent message has been drained by the peer core, in which case it internally waits until that drain completes.

M4's mbox rx callback runs in ISR context. When `mbox_send_dt()` was called from this context and that call blocked, the ISR never returned — it froze permanently — and from that moment on, the M4 core's entire interrupt handling and scheduler stopped. This is fundamentally the same class of bug confirmed back in Lab 03 — "calling `k_msleep()` inside an ISR freezes the entire core" — except this time, the culprit wasn't an obviously dangerous-looking function like `k_msleep()`, but `mbox_send_dt()`, something that looks like a "lightweight API call" on the surface.

And because the entire core (scheduler included) had stopped, even the deferred logging thread responsible for actually flushing logs to the console could no longer be scheduled. This is the exact mechanism behind the phenomenon that made this bug especially hard to diagnose: adding a diagnostic log to the callback produced no output whatsoever. The log call itself may have executed (or execution may have already frozen right before it, inside `mbox_send_dt()`) — but the thread that would actually write that content out over UART never woke up again.

## Fix

**Conclusion: `mbox_send_dt()` must not be assumed to be non-blocking.**

Lab 07's M4-side code was rewritten to follow the same **ISR → `k_msgq` → worker thread** pattern used, without exception, in every other lab.

- `mbox_rx_callback()` now only copies the received data into a `struct echo_item` and enqueues it with `k_msgq_put(&echo_msgq, &item, K_NO_WAIT)`, then returns immediately. There is no `mbox_send_dt()` call left anywhere in this function.
- The actual `mbox_send_dt()` call (and any blocking it may trigger internally) now happens only in a newly added worker thread, `Echo_Task` (`echo_task_entry()`). This thread waits on the queue with `k_msgq_get(&echo_msgq, &item, K_FOREVER)`, and only calls `mbox_send_dt()` once an item arrives. Because this is thread context, even if this call blocks, it doesn't affect the scheduling of any other thread — including the logging thread.

After the fix, re-verification on real hardware confirmed that both the M55 and M4 logs kept advancing without interruption, once every 2 seconds, and the same hang did not recur even over extended run times.

## Lesson for the Entire Curriculum

This case study is reflected across this curriculum (Chapter 0 / Section 3.2 / Chapter 12, etc.) in the following form:

> Inside an mbox callback, not only explicit blocking calls like `k_msleep()`, but also calls that look lightweight on the surface — like `mbox_send_dt()` — can block depending on the driver implementation. Therefore, **an mbox rx callback must always follow the ISR → `k_msgq` → worker thread pattern, without exception.** The judgment call "this much is lightweight enough to allow as an exception inside the callback" is no longer permitted anywhere in this project.

Some earlier lab documents had retained language along the lines of "logging output, or a certain API call, is fine to handle in an ISR." Following this case, that kind of exception was retired across the board. The only operations still permitted are extremely lightweight ones with an explicit non-blocking guarantee — such as `strcmp` or `LOG_INF` — and everything else, including any mbox send, must be delegated to a worker thread without exception.
