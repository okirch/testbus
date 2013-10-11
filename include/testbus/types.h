
#ifndef __NI_TESTBUS_TYPES_H__
#define __NI_TESTBUS_TYPES_H__

enum {
	NI_TESTBUS_PROCESS_NONSTARTER,
	NI_TESTBUS_PROCESS_EXITED,
	NI_TESTBUS_PROCESS_CRASHED,
	NI_TESTBUS_PROCESS_TRANSCENDED,	/* anything else */
};

typedef struct ni_testbus_process_exit_status  ni_testbus_process_exit_status_t;

#endif /* __NI_TESTBUS_TYPES_H__ */
