
#ifndef __TESTBUS_SERVER_TYPES_H__
#define __TESTBUS_SERVER_TYPES_H__

typedef struct ni_testbus_env		ni_testbus_env_t;
typedef struct ni_testbus_host		ni_testbus_host_t;
typedef struct ni_testbus_container	ni_testbus_container_t;
typedef struct ni_testbus_cmdqueue	ni_testbus_cmdqueue_t;
typedef struct ni_testbus_command	ni_testbus_command_t;
typedef struct ni_testbus_process	ni_testbus_process_t;
typedef struct ni_testbus_tmpfile	ni_testbus_tmpfile_t;
typedef struct ni_testbus_fileset	ni_testbus_fileset_t;
typedef struct ni_testbus_testset	ni_testbus_testset_t;
typedef struct ni_testbus_testcase	ni_testbus_testcase_t;

typedef struct ni_testbus_host_array	ni_testbus_host_array_t;
typedef struct ni_testbus_command_array	ni_testbus_command_array_t;
typedef struct ni_testbus_process_array	ni_testbus_process_array_t;
typedef struct ni_testbus_file_array	ni_testbus_file_array_t;

#define ni_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define ni_container_of(ptr, TYPE, MEMBER) ({            \
		 const typeof( ((TYPE *)0)->MEMBER ) *__mptr = (ptr);    \
		 (TYPE *)( (char *)__mptr - ni_offsetof(TYPE,MEMBER) );})

/*
 * Array/list types
 */
struct ni_testbus_cmdqueue {
	ni_testbus_command_t *		head;
};

struct ni_testbus_fileset {
	ni_testbus_tmpfile_t *		head;
};

struct ni_testbus_testset {
	ni_testbus_testcase_t *		head;
};

struct ni_testbus_host_array {
	unsigned int			count;
	ni_testbus_host_t **		data;
};

struct ni_testbus_command_array {
	unsigned int			count;
	ni_testbus_command_t **		data;
};

struct ni_testbus_process_array {
	unsigned int			count;
	ni_testbus_process_t **		data;
};

struct ni_testbus_file_array {
	unsigned int			count;
	ni_testbus_tmpfile_t **		data;
};

#endif /* __TESTBUS_SERVER_TYPES_H__ */

