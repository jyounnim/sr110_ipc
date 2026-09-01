#ifndef LAB03_IPC_COMMON_H
#define LAB03_IPC_COMMON_H

#include <stdint.h>

/* Shared by both directions: ping (M55->M4) and pong (M4->M55) both use this one struct. */
struct ipc_pingpong_msg {
	uint32_t seq;
};

#endif
