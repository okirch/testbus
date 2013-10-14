
#include <dborb/logging.h>
#include <testbus/file.h>

#include "container.h"
#include "host.h"
#include "fileset.h"
#include "command.h"
#include "testcase.h"

static ni_testbus_container_t *		__ni_testbus_global_context;

static struct ni_testbus_container_ops	ni_testbus_global_context_ops = {
	.features = ~0,
};

ni_testbus_container_t *
ni_testbus_global_context(void)
{
	if (__ni_testbus_global_context == NULL) {
		__ni_testbus_global_context = ni_malloc(sizeof(*__ni_testbus_global_context));
		__ni_testbus_global_context->ops = &ni_testbus_global_context_ops;
	}
	return __ni_testbus_global_context;
}

void
ni_testbus_container_init(ni_testbus_container_t *container, const struct ni_testbus_container_ops *ops,
			const char *name, ni_testbus_container_t *parent)
{
	ni_assert(parent);

	memset(container, 0, sizeof(*container));
	container->ops = ops;
	container->parent = parent;
	container->refcount = 1;
	ni_string_dup(&container->name, name);

	ni_testbus_env_init(&container->env);
	ni_testbus_command_array_init(&container->commands);
	ni_testbus_process_array_init(&container->processes);
	ni_testbus_host_array_init(&container->hosts);
	ni_testbus_file_array_init(&container->files);
	ni_testbus_test_array_init(&container->tests);

	/* We should have a switch statement here; this cascade of calls is awkward */
	if (ni_testbus_container_isa_host(container)) {
		ni_assert(ni_testbus_container_has_hosts(parent));
		container->id = parent->hosts.next_id++;
		ni_testbus_host_array_append(&parent->hosts, ni_testbus_host_cast(container));
	} else
	if (ni_testbus_container_isa_testcase(container)) {
		ni_assert(ni_testbus_container_has_tests(parent));
		container->id = parent->tests.next_id++;
		ni_testbus_test_array_append(&parent->tests, ni_testbus_testcase_cast(container));
	} else
	if (ni_testbus_container_isa_command(container)) {
		ni_assert(ni_testbus_container_has_commands(parent));
		container->id = parent->commands.next_id++;
		ni_testbus_command_array_append(&parent->commands, ni_testbus_command_cast(container));
	} else
	if (ni_testbus_container_isa_process(container)) {
		ni_assert(ni_testbus_container_has_processes(parent));
		container->id = parent->processes.next_id++;
		ni_testbus_process_array_append(&parent->processes, ni_testbus_process_cast(container));
	} else {
		ni_fatal("Don't know how to init container ID");
	}

	/* Above, we added the new object to its parent container, which bumped
	 * its refcount to 2. Now it's safe to drop it back to 1. */
	ni_testbus_container_put(container);

	ni_assert(container->refcount);
}

ni_testbus_container_t *
ni_testbus_container_get(ni_testbus_container_t *container)
{
	ni_assert(container->refcount); /* You cannot bring object back from the dead */

	container->refcount++;
	return container;
}

void
ni_testbus_container_put(ni_testbus_container_t *container)
{
	ni_assert(container->refcount);
	if (--(container->refcount) != 0)
		return;

	if (container->owner != NULL)
		ni_fatal("destroying container object still owned by other container");

	ni_testbus_container_destroy(container);

	if (container->ops->free)
		container->ops->free(container);
	else
		free(container);
}

void
ni_testbus_container_destroy(ni_testbus_container_t *container)
{

	if (container->ops->destroy)
		container->ops->destroy(container);

	if (container->hosts.count) {
		unsigned int i;

		for (i = 0; i < container->hosts.count; ++i) {
			ni_testbus_host_t *host = container->hosts.data[i];
			ni_dbus_object_t *host_object, *child;

			if (host->context.owner != container)
				continue;

			/* Release this host */
			ni_testbus_host_set_role(host, NULL, NULL);

#if 0
			/* This is bad. We need a diferent way of discarding stale processes */
			host_object = ni_objectmodel_object_by_path(ni_testbus_host_full_path(host));
			if (host_object) {
				child = ni_dbus_object_lookup(host_object, "Process");
				if (child)
					ni_dbus_server_object_unregister(child);
				ni_testbus_process_array_destroy(&host->container.processes);
			}
#endif
		}
	}

	ni_testbus_env_destroy(&container->env);
	ni_testbus_command_array_destroy(&container->commands);
	ni_testbus_process_array_destroy(&container->processes);
	ni_testbus_host_array_destroy(&container->hosts);
	ni_testbus_test_array_destroy(&container->tests);
	ni_testbus_file_array_destroy(&container->files);

	ni_string_free(&container->name);
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
						host->context.name);
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

/*
 * File registration/lookup
 */
void
ni_testbus_container_add_file(ni_testbus_container_t *container, ni_testbus_file_t *file)
{
	ni_assert(ni_testbus_container_has_files(container));
	ni_testbus_file_array_append(&container->files, file);
}

void
ni_testbus_container_remove_file(ni_testbus_container_t *container, ni_testbus_file_t *file)
{
	ni_assert(ni_testbus_container_has_files(container));
	ni_testbus_file_array_remove(&container->files, file);
}

ni_testbus_file_t *
ni_testbus_container_get_file_by_name(ni_testbus_container_t *container, const char *name)
{
	for (; container; container = container->parent) {
		ni_testbus_file_t *file;

		if (ni_testbus_container_has_files(container)) {
			file = ni_testbus_file_array_find_by_name(&container->files, name);
			if (file)
				return file;
		}
	}
	return NULL;
}

/*
 * Test registration/lookup
 */
void
ni_testbus_container_add_test(ni_testbus_container_t *container, ni_testbus_testcase_t *test)
{
	ni_assert(ni_testbus_container_has_tests(container));
	ni_testbus_test_array_append(&container->tests, test);
}

void
ni_testbus_container_remove_test(ni_testbus_container_t *container, ni_testbus_testcase_t *test)
{
	ni_assert(ni_testbus_container_has_tests(container));
	ni_testbus_test_array_remove(&container->tests, test);
}


ni_testbus_testcase_t *
ni_testbus_container_get_test_by_name(ni_testbus_container_t *container, const char *name)
{
	for (; container; container = container->parent) {
		ni_testbus_testcase_t *test;

		if (ni_testbus_container_has_tests(container)) {
			test = ni_testbus_test_array_find_by_name(&container->tests, name);
			if (test)
				return test;
		}
	}
	return NULL;
}
