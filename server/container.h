
#ifndef __SERVER_CONTAINER_H__
#define __SERVER_CONTAINER_H__

#include "types.h"
#include "environ.h"

enum {
	NI_TESTBUS_CONTAINER_HAS_ENV		= 0x0001,
	NI_TESTBUS_CONTAINER_HAS_CMDS		= 0x0002,
	NI_TESTBUS_CONTAINER_HAS_FILES		= 0x0004,
	NI_TESTBUS_CONTAINER_HAS_HOSTS		= 0x0008,
	NI_TESTBUS_CONTAINER_HAS_TESTS		= 0x0010,
	NI_TESTBUS_CONTAINER_HAS_PROCS		= 0x0020,
	NI_TESTBUS_CONTAINER_HAS_MONITORS	= 0x0040,
};

struct ni_testbus_container_ops {
	unsigned int			features;

	/* DBus object name prefix, such as "Host" or "File".
	 * Together with the objects's ID, his is used to construct
	 * its DBus object path, ie. /Foo/Bar/Baz/Host0 or
	 * /Foo/Bar/Baz/File17. */
	const char *			dbus_name_prefix;

	/* This is called when the object is "claimed" by some other object.
	 * Typical example is claiming a host for a test case. */
	ni_bool_t			(*claim)(ni_testbus_container_t *, ni_testbus_container_t *owner);

	/* This is called when the container "owning" the object goes away. */
	void				(*release)(ni_testbus_container_t *);

	/* This is called when we destroy an object.
	 * All generic container members are still intact at this time. */
	void				(*destroy)(ni_testbus_container_t *);

	/* This is called when we delete an object.
	 * If this is NULL, the object is deleted by free()ing the container */
	void				(*free)(ni_testbus_container_t *);
};

struct ni_testbus_container {
	const struct ni_testbus_container_ops *ops;

	char *				trace_name;

	char *				dbus_object_path;

	/* Reference counting on containers */
	unsigned int			refcount;

	/* Object ID, unique relative to object's class and the parent object.
	 * This allows hosts to be named Host0, Host1, ...
	 * and tests to be named Test0, Test1, ... */
	unsigned int			id;

	/* Object name, unique relative to object's class and the parent object.
	 * Most but not all objects have a name associated with them.
	 * Objects that do have a name include hosts, test cases, files.
	 */
	char *				name;

	/* The parent is the container this object is a child of.
	 * The owner is the container that "owns" this object.
	 *
	 * As an example, consider a Process object. This is always
	 * a child of a Host object - this is its container.
	 * However, the process is created when we run a command as
	 * part of a Test case. When the Test is destroyed, we should
	 * also clean up all Process objects that it may have left
	 * lying around - so the Test "owns" the Process.
	 */
	ni_testbus_container_t *	parent;
	ni_testbus_container_t *	owner;

	ni_testbus_env_t		env;
	ni_testbus_command_array_t	commands;
	ni_testbus_process_array_t	processes;
	ni_testbus_host_array_t		hosts;
	ni_testbus_file_array_t		files;
	ni_testbus_test_array_t		tests;
	ni_testbus_monitor_array_t	monitors;
};

typedef struct ni_testbus_container_array ni_testbus_container_array_t;
struct ni_testbus_container_array {
	unsigned int			count;
	ni_testbus_container_t **	data;
};
#define NI_TESTBUS_CONTAINER_ARRAY_INIT	{ .count = 0, .data = 0 }

extern ni_testbus_container_t *ni_testbus_global_context(void);

extern void		ni_testbus_container_init(ni_testbus_container_t *container,
						const struct ni_testbus_container_ops *ops,
						const char *name,
						ni_testbus_container_t *parent);
extern void		ni_testbus_container_destroy(ni_testbus_container_t *);
extern void		ni_testbus_container_set_owner(ni_testbus_container_t *container, ni_testbus_container_t *owner);
extern ni_bool_t	ni_testbus_container_merge_environment(ni_testbus_container_t *, ni_testbus_env_t *);
extern ni_bool_t	ni_testbus_container_merge_files(ni_testbus_container_t *, ni_testbus_file_array_t *);

extern void		ni_testbus_container_unregister(ni_testbus_container_t *);

extern ni_testbus_container_t *	ni_testbus_container_get(ni_testbus_container_t *);
extern void		ni_testbus_container_put(ni_testbus_container_t *);

extern void		ni_testbus_container_add_host(ni_testbus_container_t *, ni_testbus_host_t *);
extern void		ni_testbus_container_remove_host(ni_testbus_container_t *, ni_testbus_host_t *);
extern ni_testbus_host_t *ni_testbus_container_get_host_by_name(ni_testbus_container_t *, const char *);
extern ni_testbus_host_t *ni_testbus_container_get_host_by_role(ni_testbus_container_t *, const char *);

extern void		ni_testbus_container_add_command(ni_testbus_container_t *, ni_testbus_command_t *);

extern void		ni_testbus_container_add_file(ni_testbus_container_t *, ni_testbus_file_t *);
extern void		ni_testbus_container_remove_file(ni_testbus_container_t *, ni_testbus_file_t *);
extern ni_testbus_file_t *ni_testbus_container_get_file_by_name(ni_testbus_container_t *, const char *);
ni_testbus_file_t *	__ni_testbus_container_get_file_by_name(ni_testbus_container_t *, const char *);

extern void		ni_testbus_container_add_test(ni_testbus_container_t *, ni_testbus_testcase_t *);
extern void		ni_testbus_container_remove_test(ni_testbus_container_t *, ni_testbus_testcase_t *);
extern ni_testbus_testcase_t *ni_testbus_container_get_test_by_name(ni_testbus_container_t *, const char *);

extern ni_testbus_host_t *ni_testbus_container_find_agent_host(ni_testbus_container_t *container, const char *dbus_name);
extern void		ni_testbus_container_notify_agent_exit(ni_testbus_container_t *, const char *);

extern ni_bool_t	ni_testbus_container_isa_host(const ni_testbus_container_t *);
extern ni_bool_t	ni_testbus_container_isa_testcase(const ni_testbus_container_t *);
extern ni_bool_t	ni_testbus_container_isa_command(const ni_testbus_container_t *);
extern ni_bool_t	ni_testbus_container_isa_process(const ni_testbus_container_t *);
extern ni_bool_t	ni_testbus_container_isa_monitor(const ni_testbus_container_t *);

extern void		ni_testbus_container_array_append(ni_testbus_container_array_t *, ni_testbus_container_t *);
extern void		ni_testbus_container_array_destroy(ni_testbus_container_array_t *);

static inline ni_bool_t
ni_testbus_container_has_feature(const ni_testbus_container_t *cc, unsigned int f)
{
	return !!(cc->ops->features & f);
}

static inline ni_bool_t
ni_testbus_container_has_env(const ni_testbus_container_t *cc)
{
	return ni_testbus_container_has_feature(cc, NI_TESTBUS_CONTAINER_HAS_ENV);
}

static inline ni_bool_t
ni_testbus_container_has_commands(const ni_testbus_container_t *cc)
{
	return ni_testbus_container_has_feature(cc, NI_TESTBUS_CONTAINER_HAS_CMDS);
}

static inline ni_bool_t
ni_testbus_container_has_monitors(const ni_testbus_container_t *cc)
{
	return ni_testbus_container_has_feature(cc, NI_TESTBUS_CONTAINER_HAS_MONITORS);
}

static inline ni_bool_t
ni_testbus_container_has_hosts(const ni_testbus_container_t *cc)
{
	return ni_testbus_container_has_feature(cc, NI_TESTBUS_CONTAINER_HAS_HOSTS);
}

static inline ni_bool_t
ni_testbus_container_has_files(const ni_testbus_container_t *cc)
{
	return ni_testbus_container_has_feature(cc, NI_TESTBUS_CONTAINER_HAS_FILES);
}

static inline ni_bool_t
ni_testbus_container_has_processes(const ni_testbus_container_t *cc)
{
	return ni_testbus_container_has_feature(cc, NI_TESTBUS_CONTAINER_HAS_PROCS);
}

static inline ni_bool_t
ni_testbus_container_has_tests(const ni_testbus_container_t *cc)
{
	return ni_testbus_container_has_feature(cc, NI_TESTBUS_CONTAINER_HAS_TESTS);
}

static inline void
ni_testbus_container_release(ni_testbus_container_t *cc)
{
	cc->ops->release(cc);
}

#endif /* __SERVER_CONTAINER_H__ */
