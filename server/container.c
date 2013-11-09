
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
		__ni_testbus_global_context->refcount = 1;
		ni_string_dup(&__ni_testbus_global_context->name, "globalcontext");
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

	{
		char trace_name[64];

		snprintf(trace_name, sizeof(trace_name), "%s%u", ops->dbus_name_prefix, container->id);
		ni_string_dup(&container->trace_name, trace_name);
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
	if (container->refcount > 1) {
		container->refcount -= 1;
		return;
	}

	if (container->owner != NULL) {
		ni_warn("destroying container object %s%u still owned by other container %s%u",
				container->ops->dbus_name_prefix, container->id,
				container->owner->ops->dbus_name_prefix, container->owner->id);
		container->owner = NULL;
	}

	ni_assert(container->parent == NULL);

	ni_testbus_container_destroy(container);

	ni_string_free(&container->trace_name);

	if (container->ops->free)
		container->ops->free(container);
	else
		free(container);
}

void
ni_testbus_container_set_owner(ni_testbus_container_t *container, ni_testbus_container_t *owner)
{
	ni_assert(container->owner == NULL);
	container->owner = owner;
}

/*
 * Remove a child from a container
 */
ni_bool_t
ni_testbus_container_remove_child(ni_testbus_container_t *container, ni_testbus_container_t *parent)
{
	ni_bool_t rv;

	if (ni_testbus_container_isa_host(container)) {
		rv = ni_testbus_host_array_remove(&parent->hosts, ni_testbus_host_cast(container));
	} else
	if (ni_testbus_container_isa_testcase(container)) {
		rv = ni_testbus_test_array_remove(&parent->tests, ni_testbus_testcase_cast(container));
	} else
	if (ni_testbus_container_isa_command(container)) {
		rv = ni_testbus_command_array_remove(&parent->commands, ni_testbus_command_cast(container));
	} else
	if (ni_testbus_container_isa_process(container)) {
		rv = ni_testbus_process_array_remove(&parent->processes, ni_testbus_process_cast(container));
	} else {
		ni_fatal("Don't know how to remove container from parent");
	}

	return rv;
}

/*
 * Recursively find objects owned by @owner
 */
static void
ni_testbus_container_all_children(ni_testbus_container_t *container, ni_testbus_container_array_t *result)
{
	unsigned int i;

	for (i = 0; i < container->hosts.count; ++i) {
		ni_testbus_host_t *host = container->hosts.data[i];

		ni_testbus_container_array_append(result, &host->context);
	}
	for (i = 0; i < container->tests.count; ++i) {
		ni_testbus_testcase_t *test = container->tests.data[i];

		ni_testbus_container_array_append(result, &test->context);
	}
	for (i = 0; i < container->commands.count; ++i) {
		ni_testbus_command_t *command = container->commands.data[i];

		ni_testbus_container_array_append(result, &command->context);
	}
	for (i = 0; i < container->processes.count; ++i) {
		ni_testbus_process_t *process = container->processes.data[i];

		ni_testbus_container_array_append(result, &process->context);
	}
}

static void
ni_testbus_container_release_owned(ni_testbus_container_t *container, const ni_testbus_container_t *owner)
{
	ni_testbus_container_array_t children = NI_TESTBUS_CONTAINER_ARRAY_INIT;
	unsigned int i;

	ni_testbus_container_all_children(container, &children);
	for (i = 0; i < children.count; ++i) {
		ni_testbus_container_t *child = children.data[i];

		if (child->owner == owner) {
			child->owner = NULL;

			/* The object may be unregistered/destroyed as part of this.
			 * Note, it is not being deleted - it's safe to access
			 * it afterwards. */
			if (child->ops->release)
				child->ops->release(child);
		}
		if (child->refcount > 1)
			ni_testbus_container_release_owned(child, owner);
	}
	ni_testbus_container_array_destroy(&children);
}

void
ni_testbus_container_destroy(ni_testbus_container_t *container)
{
	ni_assert(container->trace_name);
	ni_debug_testbus("%s(%s)", __func__, container->trace_name);

	ni_assert(container->trace_name);
	if (container->parent) {
		ni_testbus_container_t *parent = container->parent;

		container->parent = NULL;
		if (!ni_testbus_container_remove_child(container, parent))
			ni_warn("Unable to remove %s from container %s", container->trace_name, parent->trace_name);
	}

	ni_testbus_container_release_owned(ni_testbus_global_context(), container);
	ni_testbus_container_release_owned(container, container);

	if (container->ops->destroy)
		container->ops->destroy(container);

	ni_testbus_env_destroy(&container->env);
	ni_testbus_command_array_destroy(&container->commands);
	ni_testbus_process_array_destroy(&container->processes);
	ni_testbus_host_array_destroy(&container->hosts);
	ni_testbus_test_array_destroy(&container->tests);
	ni_testbus_file_array_destroy(&container->files);

	ni_string_free(&container->name);

	if (container->dbus_object_path)
		ni_testbus_container_unregister(container);
}

ni_testbus_host_t *
ni_testbus_container_find_agent_host(ni_testbus_container_t *container, const char *dbus_name)
{
	unsigned int i;

	if (ni_testbus_container_has_hosts(container)) {
		for (i = 0; i < container->hosts.count; ++i) {
			ni_testbus_host_t *host = container->hosts.data[i];

			if (ni_string_eq(host->agent_bus_name, dbus_name))
				return host;
		}
	}

	return NULL;
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

ni_testbus_file_t *
__ni_testbus_container_get_file_by_name(ni_testbus_container_t *container, const char *name)
{
	if (ni_testbus_container_has_files(container))
		return ni_testbus_file_array_find_by_name(&container->files, name);

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

/*
 * Container array functions
 */
void
ni_testbus_container_array_append(ni_testbus_container_array_t *array, ni_testbus_container_t *container)
{
	array->data = ni_realloc(array->data, (array->count + 1) * sizeof(array->data[0]));
	array->data[array->count++] = ni_testbus_container_get(container);
}

void
ni_testbus_container_array_destroy(ni_testbus_container_array_t *array)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i)
		ni_testbus_container_put(array->data[i]);

	free(array->data);
	memset(array, 0, sizeof(*array));
}

