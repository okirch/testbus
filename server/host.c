
#include <stdlib.h>
#include <dborb/logging.h>
#include "model.h"
#include "host.h"

static void			ni_testbus_host_release(ni_testbus_container_t *);
static void			ni_testbus_host_destroy(ni_testbus_container_t *);
static void			ni_testbus_host_free(ni_testbus_container_t *);

static struct ni_testbus_container_ops ni_testbus_host_ops = {
	.features		= NI_TESTBUS_CONTAINER_HAS_ENV|
				  NI_TESTBUS_CONTAINER_HAS_CMDS|
				  NI_TESTBUS_CONTAINER_HAS_PROCS|
				  NI_TESTBUS_CONTAINER_HAS_FILES,

	.dbus_name_prefix	= "Host",

	/* Releasing a host: clear the role field */
	.release		= ni_testbus_host_release,

	.destroy		= ni_testbus_host_destroy,
	.free			= ni_testbus_host_free,
};


ni_testbus_host_t *
ni_testbus_host_new(ni_testbus_container_t *parent, const char *name, int *err_ret)
{
	ni_testbus_host_t *host;

	ni_assert(ni_testbus_container_has_hosts(parent));

	*err_ret = -NI_ERROR_NAME_EXISTS;
	if (ni_testbus_container_get_host_by_name(parent, name) != NULL)
		return NULL;

	host = ni_malloc(sizeof(*host));
	ni_uuid_generate(&host->uuid);

	ni_testbus_container_init(&host->context,
			&ni_testbus_host_ops,
			name,
			parent);

	*err_ret = 0;
	return host;
}

ni_bool_t
ni_testbus_container_isa_host(const ni_testbus_container_t *container)
{
	return container->ops == &ni_testbus_host_ops;
}

ni_testbus_host_t *
ni_testbus_host_cast(ni_testbus_container_t *container)
{
	ni_testbus_host_t *host;

	ni_assert(container->ops == &ni_testbus_host_ops);
	host = ni_container_of(container, ni_testbus_host_t, context);
	ni_trace("ni_testbus_host_cast(%p) -> host=%p, host->context=%p",
			container, host, &host->context);
	ni_assert(&host->context == container);
	return host;
}

void
ni_testbus_host_release(ni_testbus_container_t *container)
{
	ni_testbus_host_t *host = ni_testbus_host_cast(container);

	ni_string_free(&host->role);
}

void
ni_testbus_host_destroy(ni_testbus_container_t *container)
{
	ni_testbus_host_t *host = ni_testbus_host_cast(container);

	ni_string_free(&host->agent_bus_name);
	ni_string_free(&host->role);
}

void
ni_testbus_host_free(ni_testbus_container_t *container)
{
	ni_testbus_host_t *host = ni_testbus_host_cast(container);

	free(host);
}

ni_bool_t
ni_testbus_host_set_role(ni_testbus_host_t *host, const char *role, ni_testbus_container_t *role_owner)
{
	if (role_owner == NULL) {
		ni_string_free(&host->role);
		host->context.owner = NULL;
		return TRUE;
	}
	if (host->role != NULL) {
		/* Handle re-registration gracefully */
		if (ni_string_eq(host->role, role) && host->context.owner == role_owner)
			return TRUE;
		return FALSE;
	}

	ni_string_dup(&host->role, role);
	host->context.owner = role_owner;
	return TRUE;
}

void
ni_testbus_host_add_capability(ni_testbus_host_t *host, const char *capability)
{
	if (capability &&
	    ni_string_array_index(&host->capabilities, capability) < 0)
		ni_string_array_append(&host->capabilities, capability);
}

void
ni_testbus_host_array_init(ni_testbus_host_array_t *array)
{
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_host_array_destroy(ni_testbus_host_array_t *array)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		ni_testbus_host_t *host = array->data[i];

		if (host->context.owner != NULL) {
			if (&host->context.owner->hosts == array)
				ni_testbus_host_set_role(host, NULL, NULL);
		}
		ni_testbus_host_put(host);
	}

	free(array->data);
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_host_array_append(ni_testbus_host_array_t *array, ni_testbus_host_t *host)
{
	array->data = ni_realloc(array->data, (array->count + 1) * sizeof(array->data[0]));
	array->data[array->count++] = ni_testbus_host_get(host);
}

int
ni_testbus_host_array_index(ni_testbus_host_array_t *array, const ni_testbus_host_t *host)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		if (array->data[i] == host)
			return i;
	}
	return -1;
}

ni_bool_t
ni_testbus_host_array_remove(ni_testbus_host_array_t *array, const ni_testbus_host_t *host)
{
	int index;

	if ((index = ni_testbus_host_array_index(array, host)) < 0)
		return FALSE;

	/* Drop the reference to the host */
	ni_testbus_host_put(array->data[index]);

	memmove(&array->data[index], &array->data[index+1], array->count - (index + 1));
	array->count --;
	return TRUE;
}

ni_testbus_host_t *
ni_testbus_host_array_find_by_name(ni_testbus_host_array_t *array, const char *name)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		ni_testbus_host_t *host = array->data[i];

		if (ni_string_eq(host->context.name, name))
			return host;
	}
	return NULL;
}

ni_testbus_host_t *
ni_testbus_host_array_find_by_role(ni_testbus_host_array_t *array, const char *role)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		ni_testbus_host_t *host = array->data[i];

		if (ni_string_eq(host->role, role))
			return host;
	}
	return NULL;
}

void
ni_testbus_host_array_detach_from(ni_testbus_host_array_t *array, const ni_testbus_container_t *owner)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		ni_testbus_host_t *host = array->data[i];

		if (host->context.owner == owner)
			ni_testbus_host_set_role(host, NULL, NULL);
	}
}
