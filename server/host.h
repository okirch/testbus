
#ifndef __TESTBUS_SERVER_HOST_H__
#define __TESTBUS_SERVER_HOST_H__

#include "container.h"

struct ni_testbus_host {
	unsigned int		refcount;

	char *			name;
	unsigned		id;
	ni_uuid_t		uuid;
	ni_string_array_t	capabilities;

	char *			agent_bus_name;

	ni_testbus_container_t *role_owner;
	char *			role;

	ni_testbus_container_t	context;

	ni_bool_t		connected;
};

extern ni_testbus_host_t *	ni_testbus_host_by_name(ni_testbus_host_array_t *, const char *name);
extern ni_testbus_host_t *	ni_testbus_host_by_role(ni_testbus_host_array_t *, const char *role);

extern ni_testbus_host_t *	ni_testbus_host_new(ni_testbus_container_t *parent, const char *name, int *err_ret);
extern ni_testbus_host_t *	ni_testbus_host_get(ni_testbus_host_t *);
extern void			ni_testbus_host_put(ni_testbus_host_t *);
extern ni_bool_t		ni_testbus_host_set_role(ni_testbus_host_t *, const char *, ni_testbus_container_t *);
extern void			ni_testbus_host_add_capability(ni_testbus_host_t *, const char *);
extern void			ni_testbus_host_free(ni_testbus_host_t *);

extern void			ni_testbus_host_array_init(ni_testbus_host_array_t *);
extern void			ni_testbus_host_array_destroy(ni_testbus_host_array_t *);
extern void			ni_testbus_host_array_append(ni_testbus_host_array_t *, ni_testbus_host_t *);
extern ni_bool_t		ni_testbus_host_array_remove(ni_testbus_host_array_t *, const ni_testbus_host_t *);
extern ni_testbus_host_t *	ni_testbus_host_array_find_by_name(ni_testbus_host_array_t *array, const char *name);
extern ni_testbus_host_t *	ni_testbus_host_array_find_by_role(ni_testbus_host_array_t *array, const char *role);

#endif /* __TESTBUS_SERVER_HOST_H__ */
