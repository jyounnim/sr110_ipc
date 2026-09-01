#ifndef IPC_COMMON_H_
#define IPC_COMMON_H_

#include <zephyr/kernel.h>

/* M4 -> M55: raw accelerometer (X/Y/Z) telemetry */
struct ipc_accel_msg {
	int32_t x;
	int32_t y;
	int32_t z;
	uint32_t seq;
};

#endif /* IPC_COMMON_H_ */
