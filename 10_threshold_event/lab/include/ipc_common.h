#ifndef IPC_COMMON_H_
#define IPC_COMMON_H_

#include <zephyr/kernel.h>

enum ipc10_cmd_type {
	IPC10_CMD_SET_THRESHOLD = 1,
};

/* M55 -> M4: threshold-set command */
struct ipc10_cmd_msg {
	uint32_t cmd;
	int32_t threshold; /* milli-g, taken as an absolute-value threshold */
};

/* M4 -> M55: threshold-exceeded event (sent only when exceeded, not periodic) */
struct ipc10_event_msg {
	int32_t x;
	int32_t y;
	int32_t z;
	uint32_t seq;
};

#endif /* IPC_COMMON_H_ */
