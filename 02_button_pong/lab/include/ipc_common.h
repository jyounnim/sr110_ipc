#ifndef LAB02_IPC_COMMON_H
#define LAB02_IPC_COMMON_H

#include <stdint.h>

/* M4 -> M55: just a sequence number that increments by 1 on each button press */
struct ipc_button_evt {
	uint32_t seq;
};

#endif
