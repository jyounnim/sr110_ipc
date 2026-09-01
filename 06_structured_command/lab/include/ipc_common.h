#ifndef LAB06_IPC_COMMON_H
#define LAB06_IPC_COMMON_H

#include <stdint.h>

enum ipc_cmd_type {
	IPC_CMD_SET_LED    = 1,
	IPC_CMD_GET_STATUS = 2,
};

/* M55 -> M4 command */
struct ipc_command_msg {
	uint32_t cmd;       /* enum ipc_cmd_type */
	uint32_t led_id;    /* 0 = LED0, 1 = LED1 (only used by SET_LED) */
	uint32_t led_state; /* 0/1 (only used by SET_LED) */
};

/* M4 -> M55 reply (sent only in response to GET_STATUS) */
struct ipc_status_msg {
	uint32_t led0_state;
	uint32_t led1_state;
	uint32_t button_press_count;
};

#endif
