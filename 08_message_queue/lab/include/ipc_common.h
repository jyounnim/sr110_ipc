#ifndef LAB08_IPC_COMMON_H
#define LAB08_IPC_COMMON_H

#include <stdint.h>

enum ipc_pattern_id {
	PATTERN_BLINK_LED0  = 1,
	PATTERN_BLINK_LED1  = 2,
	PATTERN_ALTERNATE   = 3,
	PATTERN_BOTH_FLASH  = 4,
};

struct ipc_pattern_msg {
	uint32_t pattern_id; /* enum ipc_pattern_id */
	uint32_t repeat;     /* number of times to repeat the pattern */
};

#endif
