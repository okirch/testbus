
#ifndef __NI_TESTBUS_TYPES_H__
#define __NI_TESTBUS_TYPES_H__

typedef struct ni_testbus_tmpfile	ni_testbus_tmpfile_t;
typedef struct ni_testbus_file_array	ni_testbus_file_array_t;

struct ni_testbus_file_array {
	unsigned int			count;
	ni_testbus_tmpfile_t **		data;
};

#endif /* __NI_TESTBUS_TYPES_H__ */
