
#include <stdlib.h>
#include <dborb/logging.h>
#include "model.h"
#include "host.h"

static unsigned int		__ni_testbus_host_next_id;

ni_testbus_host_t *
ni_testbus_host_new(ni_testbus_container_t *parent, const char *name, int *err_ret)
{
	ni_testbus_host_t *host;

	ni_assert(ni_testbus_container_has_hosts(parent));

	*err_ret = -NI_ERROR_NAME_EXISTS;
	if (ni_testbus_container_get_host_by_name(parent, name) != NULL)
		return NULL;

	host = ni_malloc(sizeof(*host));
	host->refcount = 1;
	host->name = ni_strdup(name);
	host->id = __ni_testbus_host_next_id++;
	ni_uuid_generate(&host->uuid);

	ni_testbus_container_init(&host->context,
			NI_TESTBUS_CONTAINER_HAS_ENV|
			NI_TESTBUS_CONTAINER_HAS_CMDS|
			NI_TESTBUS_CONTAINER_HAS_PROCS|
			NI_TESTBUS_CONTAINER_HAS_FILES,
			parent);

	/* This will implicitly set the host's refcount to 2 */
	ni_testbus_container_add_host(parent, host);

	/* Now unref the host */
	host->refcount--;

	*err_ret = 0;
	return host;
}

void
ni_testbus_host_free(ni_testbus_host_t *host)
{
	ni_assert(host->refcount == 0);
	ni_string_free(&host->name);
	ni_string_free(&host->agent_bus_name);
	ni_string_free(&host->role);
	ni_testbus_container_destroy(&host->context);
	free(host);
}

ni_testbus_host_t *
ni_testbus_host_get(ni_testbus_host_t *host)
{
	ni_assert(host->refcount);
	host->refcount += 1;
	return host;
}

void
ni_testbus_host_put(ni_testbus_host_t *host)
{
	ni_assert(host->refcount);
	if (--(host->refcount) == 0)
		ni_testbus_host_free(host);
}

ni_bool_t
ni_testbus_host_set_role(ni_testbus_host_t *host, const char *role, ni_testbus_container_t *role_owner)
{
	if (role_owner == NULL) {
		ni_string_free(&host->role);
		host->role_owner = NULL;
		return TRUE;
	}
	if (host->role != NULL) {
		/* Handle re-registration gracefully */
		if (ni_string_eq(host->role, role) && host->role_owner == role_owner)
			return TRUE;
		return FALSE;
	}

	ni_string_dup(&host->role, role);
	host->role_owner = role_owner;
	return TRUE;
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

		if (host->role_owner != NULL) {
			if (&host->role_owner->hosts == array)
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

		if (strcmp(host->name, name) == 0)
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

		if (strcmp(host->role, role) == 0)
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

		if (host->role_owner == owner)
			ni_testbus_host_set_role(host, NULL, NULL);
	}
}

