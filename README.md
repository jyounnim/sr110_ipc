# Astra SR110 M4↔M55 IPC Lab Curriculum

A hands-on curriculum for learning Inter-Processor Communication (IPC) between the Cortex-M55 and Cortex-M4 cores of the Synaptics Astra SR110 (board: `sr100_rdk`) — an asymmetric dual-core (AMP) SoC — using Zephyr RTOS's raw `mbox` driver API.

This document is the map for the whole curriculum. See the lesson document inside each lab's folder for the full details of that lab.

## What this curriculum is (and isn't)

- **It uses the raw `mbox` API directly.** Higher-level IPC frameworks such as Zephyr's `ipc_service` / OpenAMP are out of scope. You call the low-level API — `mbox_send_dt()`, `mbox_register_callback_dt()`, `mbox_set_enabled_dt()` — directly, with the goal of understanding how IPC actually works at the hardware level before reaching for anything higher-level.
- **Every lab follows the same build shape.** The M4 (remote) image is always built first, and its output is included in the M55 (host) build. As the labs progress, the message protocols and thread designs get more elaborate, but this build structure and the base project layout (`lab/` is M55, `lab/remote/` is M4) stay identical throughout.
- **Each lab folder has a `doc/` subfolder with two documents per language.**
  - `NN_topic_en.md`: the **lesson document**, describing the finished code as it stands today. It covers learning objectives, IPC concepts, a code walkthrough, and build/run instructions. It contains no debugging history — the final, working code is presented as if it had been designed this way from the start.
  - `NN_topic_troubleshooting_en.md`: a **troubleshooting log** of the problems actually found and fixed during hardware bring-up. It's kept separate from the lesson document on purpose: a learner meeting the lab for the first time should focus on "what is the correct design," and only reach for the troubleshooting notes if they hit a similar snag in their own setup.
  - Korean editions of both (`_kr.md`) live alongside the English ones in the same `doc/` folder.

## Hardware assumptions

- Board: `sr100_rdk` (Synaptics Astra SR110)
- M55: `sr100_rdk/sr100/m55` — the HOST core. In most labs, this is the side that kicks off the scenario.
- M4: `sr100_rdk/sr100/m4` — the CLIENT/REMOTE core. In most labs, this is the side that physically drives the peripherals (GPIO, I2C sensors, SPI, etc.).
- M4 and M55 **physically share the I2C1 bus** on this board. Because of that, every lab that touches a device on I2C1 (LEDs, buttons, the accelerometer) disables `&i2c1`, `&gpio_exp0` (and its relevant child nodes) in the M55 overlay, leaving ownership of the bus to whichever core actually drives it (usually M4). The background for this is covered in detail in the [Lab 01 troubleshooting document](01_hello_ipc/doc/01_hello_ipc_troubleshooting_en.md).
- Each core prints to its own independent UART console (230400 bps, 8N1). It's worth keeping both the M55 and M4 consoles open side by side while working through a lab, so you can see both sides of the conversation at once.

## Building (common to every lab)

Every lab in this curriculum builds from the root of the west workspace (the directory where `zephyr/` is visible), in this order. Replace `<N>_<lab_name>` with the lab folder's name.

```bash
# 1) Build the M4 (remote) image first
west build -p always -b sr100_rdk/sr100/m4 ./sr110_ipc/<N>_<lab_name>/lab/remote -d m4

# 2) Build the M55 (host) image — this pulls in the M4 binary you just built
west build -p always -b sr100_rdk/sr100/m55 ./sr110_ipc/<N>_<lab_name>/lab -d m55 \
    -DCONFIG_SR100_RELEASE_M4_RESET=y -DM4_BUILD="../m4"
```

`M4_BUILD` is resolved as a **relative path from the M55 build directory** (`-d m55`), not from your current working directory. In a layout where `m4/` and `m55/` are sibling directories under the workspace root, `../m4` is the correct value — pointing it at `./m4` fails silently (no CMake error at all), quietly leaving the M4 firmware out of the final image (M55 boots and runs fine; M4 never boots, as if it weren't there). Each lab's own document restates this exact command with lab-specific paths, so you can copy it directly. Flashing the board follows whatever standard procedure your toolchain/board uses — that's outside the scope of this document.

## IPC concepts, at a glance

Each lesson document explains a concept in depth the first time it appears. This section collects only the ideas that run through the entire curriculum — skim it before you start, and come back to it as you work through the labs.

**mbox (mailbox)** — a hardware block built into the SoC for inter-processor communication. When one core writes to a register, it raises a hardware interrupt (a "doorbell") on the other core. The mbox mechanism itself only carries the signal that "data has arrived" — the actual payload lives in a shared memory region defined in the devicetree.

**AMP (Asymmetric Multi-Processing)** — M4 and M55 don't share a single OS instance; each boots and runs its own independent Zephyr image. The two cores are, for all practical purposes, two separate computers sharing one chip — which is exactly why you need IPC instead of a function call or a shared global variable.

**ISR context and the no-blocking rule — the single most important rule in this curriculum** — a callback registered with `mbox_register_callback_dt()` runs in **interrupt service routine (ISR) context** the moment a message arrives. Inside an ISR you cannot make a blocking call that involves the scheduler (`k_msleep()` and friends), and — as hardware testing in Lab 07 proved — `mbox_send_dt()` itself can also block on this board's mbox backend, so it's banned from ISR context too, with **no exceptions**. Every lab in this curriculum follows this pattern as a result:

1. The mbox callback (running in ISR context) does nothing but copy the incoming message into a queue with `k_msgq_put(..., K_NO_WAIT)` and return immediately.
2. A separate worker thread waits on that queue with `k_msgq_get()`. When a message shows up, it's the worker thread — not the ISR — that does the real work (driving hardware, sending a reply, anything that might take time).

This "ISR → `k_msgq` → worker thread" pattern first appears in Lab 01, and every later lab reuses it in one shape or another. Why this rule is non-negotiable, and what actually happens when it's violated, is documented in detail in [Lab 03's troubleshooting notes](03_full_duplex_ping_pong/doc/03_full_duplex_ping_pong_troubleshooting_en.md) and, especially, [Lab 07's troubleshooting notes](07_echo_service/doc/07_echo_service_troubleshooting_en.md) (a real case where `mbox_send_dt()` in an ISR hung the entire system).

**Message protocol design** — the early labs (01–04) use the simplest possible message: one field, one value. Starting with Lab 06, a command-type field is used to multiplex several kinds of request/response onto a single channel. Later labs (especially Lab 18) take this further, unifying several message kinds into a single tagged-union (envelope) protocol.

**Periodic push vs. event-driven** — Lab 09 sends a sensor reading on a fixed schedule regardless of whether anything changed ("telemetry push"); Lab 10 only sends a message when a condition is met (a threshold is crossed — "event-driven"). Understanding the trade-offs between the two (bandwidth, latency, and how to handle a missed event) makes the design intent of later labs (15: Telemetry Hub, 16: Heartbeat) much clearer.

## Lab index (Labs 01–10 — verified on real hardware)

| # | Title | Direction | New concept introduced in this lab |
|---|-------|-----------|-------------------------------------|
| 01 | [Hello IPC](01_hello_ipc/doc/01_hello_ipc_en.md) | M55 → M4 | mbox fundamentals, devicetree `mbox-consumer`, the ISR→msgq→worker-thread pattern |
| 02 | [Button Pong](02_button_pong/doc/02_button_pong_en.md) | M4 → M55 | Forwarding a device event to the host, `gpio-keys` polling mode |
| 03 | [Full-Duplex Ping-Pong](03_full_duplex_ping_pong/doc/03_full_duplex_ping_pong_en.md) | M55 ↔ M4 | Simultaneous bidirectional communication; the no-blocking-in-callback rule is established |
| 04 | [Shared Counter](04_shared_counter/doc/04_shared_counter_en.md) | M55 → M4 | Synchronizing state via message passing vs. true shared memory |
| 05 | [Button Press Counter](05_button_press_counter/doc/05_button_press_counter_en.md) | M4 → M55 | Turning a repeated event into stateful, cumulative counting |
| 06 | [Structured Command](06_structured_command/doc/06_structured_command_en.md) | M55 ↔ M4 | A structured protocol that multiplexes several request types via a command field |
| 07 | [Echo Service](07_echo_service/doc/07_echo_service_en.md) | M55 ↔ M4 | **`mbox_send_dt()` is also banned from ISR context** — the curriculum's final rule is locked in |
| 08 | [Message Queue](08_message_queue/doc/08_message_queue_en.md) | M4 → M55 | Queuing a continuous message stream; sizing the queue depth |
| 09 | [Accel Telemetry](09_accel_telemetry/doc/09_accel_telemetry_en.md) | M4 → M55 | Periodic sensor telemetry; using Zephyr's sensor subsystem |
| 10 | [Threshold Event](10_threshold_event/doc/10_threshold_event_en.md) | M4 → M55 | Event-driven delivery, baseline calibration, handling commands and periodic work on one thread |

## What's coming next (Labs 11–18 — in progress)

The labs below haven't gone through this documentation pass (or hardware verification) yet, and are still in their initial design-note state. They'll be brought into the curriculum through the same process as Labs 01–10 — hardware verification, followed by the lesson/troubleshooting document split.

| # | Title | Overview |
|---|-------|----------|
| 11 | OLED Display Control | M55 → M4 → SSD1306 display control |
| 12 | AHT20 Temp/Humidity Logging | M4 → M55, temperature/humidity sensor logging |
| 13 | SPI ADC Control Loop | M55 (sensor) ↔ M4 (actuator) — hardware ownership reversed from earlier labs |
| 14 | UART Bridge | External UART → M4 → M55 bridge |
| 15 | Telemetry Hub | Combines Lab 09 and Lab 12, reuses Lab 10's single-thread pattern |
| 16 | Heartbeat Watchdog | Monitoring that M4 is still alive |
| 17 | Low Power Sync (experimental) | M55 sleep / M4 wake trigger — unverified |
| 18 | Capstone Gateway | Final integration lab that unifies every pattern above into one tagged-union (envelope) protocol |

## Recommended order

Work through the labs in numeric order. Each one adds exactly one new concept on top of the pattern the previous labs established, and three labs in particular mark the points where a core design principle of the whole curriculum gets locked in — don't skip these:

- **Lab 01**: this is where the fundamentals — mbox, devicetree, the ISR/worker-thread pattern — all get established.
- **Lab 03**: this is where you see, on real hardware, why the no-blocking-in-callback rule matters for bidirectional communication.
- **Lab 07**: this is where the curriculum's single most important lesson gets locked in — not even `mbox_send_dt()` may be called from ISR context.

If you get stuck on a lab's hardware, check that lab's troubleshooting document first. The same handful of known issues (I2C1 bus sharing, the `M4_BUILD` relative path, the GPIO expander's `polling-mode` requirement) recur across labs that share the same board/site, and are usually documented down to the root cause in whichever lab's troubleshooting notes discovered them first.
