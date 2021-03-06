
#ifndef __TESTBUS_SERVER_TYPES_H__
#define __TESTBUS_SERVER_TYPES_H__

#include <testbus/types.h>

typedef struct ni_testbus_env		ni_testbus_env_t;
typedef struct ni_testbus_host		ni_testbus_host_t;
typedef struct ni_testbus_container	ni_testbus_container_t;
typedef struct ni_testbus_command	ni_testbus_command_t;
typedef struct ni_testbus_process	ni_testbus_process_t;
typedef struct ni_testbus_monitor	ni_testbus_monitor_t;
typedef struct ni_testbus_test_array	ni_testbus_test_array_t;
typedef struct ni_testbus_testcase	ni_testbus_testcase_t;

typedef struct ni_testbus_host_array	ni_testbus_host_array_t;
typedef struct ni_testbus_command_array	ni_testbus_command_array_t;
typedef struct ni_testbus_process_array	ni_testbus_process_array_t;
typedef struct ni_testbus_monitor_array	ni_testbus_monitor_array_t;

/*
 * Array/list types
 */
struct ni_testbus_test_array {
	unsigned int			count;
	ni_testbus_testcase_t **	data;

	unsigned int			next_id;
};

struct ni_testbus_host_array {
	unsigned int			count;
	ni_testbus_host_t **		data;

	unsigned int			next_id;
};

struct ni_testbus_command_array {
	unsigned int			count;
	ni_testbus_command_t **		data;

	unsigned int			next_id;
};

struct ni_testbus_process_array {
	unsigned int			count;
	ni_testbus_process_t **		data;

	unsigned int			next_id;
};

struct ni_testbus_monitor_array {
	unsigned int			count;
	ni_testbus_monitor_t **		data;

	unsigned int			next_id;
};

#endif /* __TESTBUS_SERVER_TYPES_H__ */

