#ifndef __SERVER_MONITOR_H__
#define __SERVER_MONITOR_H__

#include <dborb/monitor.h>
#include "container.h"

struct ni_testbus_monitor {
	char *			class;
	ni_var_array_t		params;

	ni_testbus_container_t	context;
};


extern void			ni_testbus_monitor_array_init(ni_testbus_monitor_array_t *);
extern void			ni_testbus_monitor_array_destroy(ni_testbus_monitor_array_t *);
extern void			ni_testbus_monitor_array_append(ni_testbus_monitor_array_t *, ni_testbus_monitor_t *);
extern ni_bool_t		ni_testbus_monitor_array_remove(ni_testbus_monitor_array_t *, const ni_testbus_monitor_t *);

extern ni_testbus_monitor_t *	ni_testbus_monitor_new(ni_testbus_container_t *,
					const char *name, const char *class,
					ni_var_array_t *params);
extern ni_testbus_monitor_t *	ni_testbus_monitor_cast(ni_testbus_container_t *);


static inline ni_testbus_monitor_t *
ni_testbus_monitor_get(ni_testbus_monitor_t *monitor)
{
	ni_testbus_container_get(&monitor->context);
	return monitor;
}

static inline void
ni_testbus_monitor_put(ni_testbus_monitor_t *monitor)
{
	ni_testbus_container_put(&monitor->context);
}

#endif /* __SERVER_MONITOR_H__ */
