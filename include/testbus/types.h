
#ifndef __NI_TESTBUS_TYPES_H__
#define __NI_TESTBUS_TYPES_H__

#include <dborb/types.h>

typedef struct ni_testbus_file		ni_testbus_file_t;
typedef struct ni_testbus_file_array	ni_testbus_file_array_t;

enum {
	NI_TESTBUS_FILE_READ	= 0x0001,
	NI_TESTBUS_FILE_WRITE	= 0x0002,
	NI_TESTBUS_FILE_EXEC	= 0x0004,
};

struct ni_testbus_file_array {
	unsigned int			count;
	ni_testbus_file_t **		data;

	unsigned int			next_id;
};

#define NI_TESTBUS_FILE_ARRAY_INIT	{ .count = 0, .data = NULL, .next_id = 0 }

#endif /* __NI_TESTBUS_TYPES_H__ */
