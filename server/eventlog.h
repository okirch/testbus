
#ifndef __TESTBUS_SERVER_EVENTLOG_H__
#define __TESTBUS_SERVER_EVENTLOG_H__

#include <dborb/monitor.h>
#include "container.h"

struct ni_testbus_host {
	ni_string_array_t	capabilities;

	char *			agent_bus_name;
	char *			role;
	ni_eventlog_t *		eventlog;

	ni_testbus_container_t	context;

	ni_bool_t		ready;
};

extern ni_testbus_host_t *	ni_testbus_host_cast(ni_testbus_container_t *);
extern ni_testbus_host_t *	ni_testbus_host_by_name(ni_testbus_host_array_t *, const char *name);
extern ni_testbus_host_t *	ni_testbus_host_by_role(ni_testbus_host_array_t *, const char *role);

extern ni_testbus_host_t *	ni_testbus_host_new(ni_testbus_container_t *parent, const char *name, int *err_ret);
extern ni_bool_t		ni_testbus_host_set_role(ni_testbus_host_t *, const char *, ni_testbus_container_t *);
extern void			ni_testbus_host_add_capability(ni_testbus_host_t *, const char *);

extern void			ni_testbus_host_agent_ready(ni_testbus_host_t *);
extern void			ni_testbus_host_agent_disconnected(ni_testbus_host_t *);

extern void			ni_testbus_host_array_init(ni_testbus_host_array_t *);
extern void			ni_testbus_host_array_destroy(ni_testbus_host_array_t *);
extern void			ni_testbus_host_array_append(ni_testbus_host_array_t *, ni_testbus_host_t *);
extern ni_bool_t		ni_testbus_host_array_remove(ni_testbus_host_array_t *, const ni_testbus_host_t *);
extern ni_testbus_host_t *	ni_testbus_host_array_find_by_name(ni_testbus_host_array_t *array, const char *name);
extern ni_testbus_host_t *	ni_testbus_host_array_find_by_role(ni_testbus_host_array_t *array, const char *role);

static inline ni_testbus_host_t *
ni_testbus_host_get(ni_testbus_host_t *host)
{
	ni_testbus_container_get(&host->context);
	return host;
}

static inline void
ni_testbus_host_put(ni_testbus_host_t *host)
{
	ni_testbus_container_put(&host->context);
}

#endif /* __TESTBUS_SERVER_EVENTLOG_H__ */
