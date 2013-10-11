
#include <dborb/logging.h>
#include <testbus/file.h>

#include "container.h"
#include "host.h"
#include "fileset.h"
#include "command.h"
#include "testcase.h"

static ni_testbus_container_t *		__ni_testbus_global_context;

ni_testbus_container_t *
ni_testbus_global_context(void)
{
	if (__ni_testbus_global_context == NULL) {
		__ni_testbus_global_context = ni_malloc(sizeof(*__ni_testbus_global_context));
		__ni_testbus_global_context->features = ~0;
	}
	return __ni_testbus_global_context;
}

void
ni_testbus_container_init(ni_testbus_container_t *container, unsigned int features, ni_testbus_container_t *parent)
{
	memset(container, 0, sizeof(*container));
	container->features = features;
	container->parent = parent;

	ni_testbus_env_init(&container->env);
	ni_testbus_command_array_init(&container->commands);
	ni_testbus_process_array_init(&container->processes);
	ni_testbus_host_array_init(&container->hosts);
	ni_testbus_file_array_init(&container->files);
	ni_testbus_testset_init(&container->tests);
}

void
ni_testbus_container_destroy(ni_testbus_container_t *container)
{
	ni_testbus_env_destroy(&container->env);
	ni_testbus_command_array_destroy(&container->commands);
	ni_testbus_process_array_destroy(&container->processes);
	ni_testbus_host_array_destroy(&container->hosts);
	ni_testbus_testset_destroy(&container->tests);
	ni_testbus_file_array_destroy(&container->files);
}

void
ni_testbus_container_notify_agent_exit(ni_testbus_container_t *container, const char *dbus_name)
{
	unsigned int i;

	if (ni_testbus_container_has_hosts(container)) {
		for (i = 0; i < container->hosts.count; ++i) {
			ni_testbus_host_t *host = container->hosts.data[i];

			if (ni_string_eq(host->agent_bus_name, dbus_name)) {
				ni_debug_wicked("host %s - owning agent disconnected",
						host->name);
				ni_string_free(&host->agent_bus_name);
			}
		}
	}
}

/*
 * Environment merging
 */
ni_bool_t
ni_testbus_container_merge_environment(ni_testbus_container_t *container, ni_testbus_env_t *result)
{
	ni_testbus_env_t tmp_result = NI_TESTBUS_ENV_INIT;
	ni_testbus_env_array_t env_array = NI_TESTBUS_ENV_ARRAY_INIT;
	ni_bool_t rv = TRUE;

	if (result->vars.count)
		ni_testbus_env_array_append(&env_array, result);

	while (container) {
		if (ni_testbus_container_has_env(container)) {
			if (container->env.vars.count)
				ni_testbus_env_array_append(&env_array, &container->env);
		}

		container = container->parent;
	}

	if (env_array.count) {
		rv = ni_testbus_env_merge(&tmp_result, &env_array);
		if (rv) {
			ni_testbus_env_destroy(result);
			*result = tmp_result;
			memset(&tmp_result, 0, sizeof(tmp_result));
		}
	}

	ni_testbus_env_array_destroy(&env_array);
	ni_testbus_env_destroy(&tmp_result);

	return rv;
}

ni_bool_t
ni_testbus_container_merge_files(ni_testbus_container_t *container, ni_testbus_file_array_t *result)
{
	while (container) {
		if (ni_testbus_container_has_files(container))
			ni_testbus_file_array_merge(result, &container->files);

		container = container->parent;
	}

	return TRUE;
}

/*
 * Host registration/lookup
 */
void
ni_testbus_container_add_host(ni_testbus_container_t *container, ni_testbus_host_t *host)
{
	ni_assert(ni_testbus_container_has_hosts(container));
	ni_testbus_host_array_append(&container->hosts, host);
}

void
ni_testbus_container_remove_host(ni_testbus_container_t *container, ni_testbus_host_t *host)
{
	ni_assert(ni_testbus_container_has_hosts(container));
	ni_testbus_host_array_remove(&container->hosts, host);
}

ni_testbus_host_t *
ni_testbus_container_get_host_by_name(ni_testbus_container_t *container, const char *name)
{
	for (; container; container = container->parent) {
		ni_testbus_host_t *host;

		if (ni_testbus_container_has_hosts(container)) {
			host = ni_testbus_host_array_find_by_name(&container->hosts, name);
			if (host)
				return host;
		}
	}
	return NULL;
}

ni_testbus_host_t *
ni_testbus_container_get_host_by_role(ni_testbus_container_t *container, const char *role)
{
	for (; container; container = container->parent) {
		ni_testbus_host_t *host;

		if (ni_testbus_container_has_hosts(container)) {
			host = ni_testbus_host_array_find_by_role(&container->hosts, role);
			if (host)
				return host;
		}
	}
	return NULL;
}

void
ni_testbus_container_add_file(ni_testbus_container_t *container, ni_testbus_tmpfile_t *file)
{
	ni_assert(ni_testbus_container_has_files(container));
	ni_testbus_file_array_append(&container->files, file);
}

#if 0
void
ni_testbus_container_remove_file(ni_testbus_container_t *container, ni_testbus_tmpfile_t *file)
{
	ni_assert(ni_testbus_container_has_files(container));
	ni_testbus_file_array_remove(&container->files, file);
}
#endif

ni_testbus_tmpfile_t *
ni_testbus_container_get_file_by_name(ni_testbus_container_t *container, const char *name)
{
	for (; container; container = container->parent) {
		ni_testbus_tmpfile_t *file;

		if (ni_testbus_container_has_files(container)) {
			file = ni_testbus_file_array_find_by_name(&container->files, name);
			if (file)
				return file;
		}
	}
	return NULL;
}

ni_testbus_testcase_t *
ni_testbus_container_get_test_by_name(ni_testbus_container_t *container, const char *name)
{
	for (; container; container = container->parent) {
		ni_testbus_testcase_t *test;

		if (ni_testbus_container_has_tests(container)) {
			test = ni_testbus_testset_find_by_name(&container->tests, name);
			if (test)
				return test;
		}
	}
	return NULL;
}
