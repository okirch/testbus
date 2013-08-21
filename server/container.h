
#ifndef __SERVER_CONTAINER_H__
#define __SERVER_CONTAINER_H__

#include "types.h"
#include "environ.h"

enum {
	NI_TESTBUS_CONTAINER_HAS_ENV	= 0x0001,
	NI_TESTBUS_CONTAINER_HAS_CMDS	= 0x0002,
	NI_TESTBUS_CONTAINER_HAS_FILES	= 0x0004,
	NI_TESTBUS_CONTAINER_HAS_HOSTS	= 0x0008,
	NI_TESTBUS_CONTAINER_HAS_TESTS	= 0x0010,
	NI_TESTBUS_CONTAINER_HAS_PROCS	= 0x0020,
};

struct ni_testbus_container {
	unsigned int			features;

	ni_testbus_container_t *	parent;

	ni_testbus_env_t		env;
	ni_testbus_command_array_t	commands;
	ni_testbus_process_array_t	processes;
	ni_testbus_host_array_t		hosts;
	ni_testbus_file_array_t		files;
	ni_testbus_testset_t		tests;
};

extern ni_testbus_container_t *ni_testbus_global_context(void);

extern void		ni_testbus_container_init(ni_testbus_container_t *container,
						unsigned int features,
						ni_testbus_container_t *parent);
extern void		ni_testbus_container_destroy(ni_testbus_container_t *);
extern ni_bool_t	ni_testbus_container_merge_environment(ni_testbus_container_t *, ni_testbus_env_t *);
extern ni_bool_t	ni_testbus_container_merge_files(ni_testbus_container_t *, ni_testbus_file_array_t *);

extern void		ni_testbus_container_add_host(ni_testbus_container_t *, ni_testbus_host_t *);
extern void		ni_testbus_container_remove_host(ni_testbus_container_t *, ni_testbus_host_t *);
extern ni_testbus_host_t *ni_testbus_container_get_host_by_name(ni_testbus_container_t *, const char *);
extern ni_testbus_host_t *ni_testbus_container_get_host_by_role(ni_testbus_container_t *, const char *);

extern void		ni_testbus_container_add_command(ni_testbus_container_t *, ni_testbus_command_t *);

extern void		ni_testbus_container_add_file(ni_testbus_container_t *, ni_testbus_tmpfile_t *);
extern void		ni_testbus_container_remove_file(ni_testbus_container_t *, ni_testbus_tmpfile_t *);
extern ni_testbus_tmpfile_t *ni_testbus_container_get_file_by_name(ni_testbus_container_t *, const char *);

extern void		ni_testbus_container_add_test(ni_testbus_container_t *, ni_testbus_testcase_t *);
extern void		ni_testbus_container_remove_test(ni_testbus_container_t *, ni_testbus_testcase_t *);
extern ni_testbus_testcase_t *ni_testbus_container_get_test_by_name(ni_testbus_container_t *, const char *);

extern void		ni_testbus_container_notify_agent_exit(ni_testbus_container_t *, const char *);

static inline ni_bool_t
ni_testbus_container_has_env(const ni_testbus_container_t *cc)
{
	return !!(cc->features & NI_TESTBUS_CONTAINER_HAS_ENV);
}

static inline ni_bool_t
ni_testbus_container_has_commands(const ni_testbus_container_t *cc)
{
	return !!(cc->features & NI_TESTBUS_CONTAINER_HAS_CMDS);
}

static inline ni_bool_t
ni_testbus_container_has_hosts(const ni_testbus_container_t *cc)
{
	return !!(cc->features & NI_TESTBUS_CONTAINER_HAS_HOSTS);
}

static inline ni_bool_t
ni_testbus_container_has_files(const ni_testbus_container_t *cc)
{
	return !!(cc->features & NI_TESTBUS_CONTAINER_HAS_FILES);
}

static inline ni_bool_t
ni_testbus_container_has_processes(const ni_testbus_container_t *cc)
{
	return !!(cc->features & NI_TESTBUS_CONTAINER_HAS_PROCS);
}

static inline ni_bool_t
ni_testbus_container_has_tests(const ni_testbus_container_t *cc)
{
	return !!(cc->features & NI_TESTBUS_CONTAINER_HAS_TESTS);
}

#endif /* __SERVER_CONTAINER_H__ */
