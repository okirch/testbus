
#include <stdlib.h>
#include <dborb/logging.h>
#include <dborb/process.h>
#include "command.h"

static void			ni_testbus_command_destroy(ni_testbus_container_t *);
static void			ni_testbus_command_free(ni_testbus_container_t *);

static struct ni_testbus_container_ops ni_testbus_command_ops = {
	.features		= NI_TESTBUS_CONTAINER_HAS_ENV |
		    		  NI_TESTBUS_CONTAINER_HAS_FILES,
	.dbus_name_prefix	= "Command",

	.destroy		= ni_testbus_command_destroy,
	.free			= ni_testbus_command_free,
};

static struct ni_testbus_container_ops ni_testbus_process_ops = {
	.features		= NI_TESTBUS_CONTAINER_HAS_ENV |
				  NI_TESTBUS_CONTAINER_HAS_FILES,

	.dbus_name_prefix	= "Process",
};


void
ni_testbus_command_array_init(ni_testbus_command_array_t *array)
{
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_command_array_destroy(ni_testbus_command_array_t *array)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		ni_testbus_command_t *cmd = array->data[i];

		ni_testbus_command_put(cmd);
	}
	free(array->data);
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_command_array_append(ni_testbus_command_array_t *array, ni_testbus_command_t *command)
{
	array->data = ni_realloc(array->data, (array->count + 1) * sizeof(array->data[0]));
	array->data[array->count++] = ni_testbus_command_get(command);
}

ni_testbus_command_t *
ni_testbus_command_new(ni_testbus_container_t *parent, const ni_string_array_t *argv)
{
	ni_testbus_command_t *cmd;

	ni_assert(parent);
	ni_assert(ni_testbus_container_has_commands(parent));

	cmd = ni_malloc(sizeof(*cmd));
	ni_string_array_copy(&cmd->argv, argv);

	ni_testbus_container_init(&cmd->context,
			&ni_testbus_command_ops,
			NULL,
			parent);

	return cmd;
}

ni_bool_t
ni_testbus_container_isa_command(const ni_testbus_container_t *container)
{
	return container->ops == &ni_testbus_command_ops;
}

ni_testbus_command_t *
ni_testbus_command_cast(ni_testbus_container_t *container)
{
	ni_assert(container->ops == &ni_testbus_command_ops);
	return ni_container_of(container, ni_testbus_command_t, context);
}

void
ni_testbus_command_destroy(ni_testbus_container_t *container)
{
	ni_testbus_command_t *cmd = ni_testbus_command_cast(container);

	ni_string_array_destroy(&cmd->argv);
}

void
ni_testbus_command_free(ni_testbus_container_t *container)
{
	ni_testbus_command_t *cmd = ni_testbus_command_cast(container);

	free(cmd);
}

/*
 * Process handling
 * A process is a command in execution. There is no 1:1 relationship, as a command
 * may be executed on several hosts simultanously.
 */
ni_testbus_process_t *
ni_testbus_process_new(ni_testbus_container_t *parent, ni_testbus_command_t *command)
{
	ni_testbus_process_t *proc;

	ni_assert(parent);
	ni_assert(command);
	ni_assert(ni_testbus_container_has_processes(parent));

	proc = ni_malloc(sizeof(*proc));
	proc->command = ni_testbus_command_get(command);
	ni_string_array_copy(&proc->argv, &command->argv);

	ni_testbus_container_init(&proc->context,
			&ni_testbus_process_ops,
			NULL,
			parent);

	return proc;
}

ni_bool_t
ni_testbus_container_isa_process(const ni_testbus_container_t *container)
{
	return container->ops == &ni_testbus_process_ops;
}

ni_testbus_process_t *
ni_testbus_process_cast(ni_testbus_container_t *container)
{
	ni_assert(container->ops == &ni_testbus_process_ops);
	return ni_container_of(container, ni_testbus_process_t, context);
}

void
ni_testbus_process_destroy(ni_testbus_container_t *container)
{
	ni_testbus_process_t *proc = ni_testbus_process_cast(container);

	/* FIXME: We should broadcast a signal informing the agent that he
	 * can drop the process status, too */

	ni_string_array_destroy(&proc->argv);
	if (proc->command) {
		ni_testbus_command_put(proc->command);
		proc->command = NULL;
	}

	if (proc->process)
		ni_process_free(proc->process);
}

void
ni_testbus_process_free(ni_testbus_container_t *container)
{
	ni_testbus_process_t *proc = ni_testbus_process_cast(container);

	free(proc);
}

void
ni_testbus_process_apply_context(ni_testbus_process_t *proc, ni_testbus_container_t *container)
{
	ni_testbus_container_merge_environment(container, &proc->context.env);
	ni_testbus_container_merge_files(container, &proc->context.files);
}

/*
 * Functionality for async execution of subprocesses
 */
ni_bool_t
ni_testbus_process_run(ni_testbus_process_t *proc, void (*callback)(ni_process_t *), void *user_data)
{
	ni_process_t *pi;
	int rv;

	proc->process = pi = ni_process_new_ext(&proc->argv, &proc->context.env.vars);
	if (proc->process == NULL) {
		ni_error("unable to create process object");
		return FALSE;
	}

	rv = ni_process_run(pi);
	if (rv < 0)
		return FALSE;

	pi->notify_callback = callback;
	pi->user_data = user_data;
	return TRUE;
}

void
ni_testbus_process_array_init(ni_testbus_process_array_t *array)
{
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_process_array_destroy(ni_testbus_process_array_t *array)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		ni_testbus_process_t *cmd = array->data[i];

		ni_testbus_process_put(cmd);
	}
	free(array->data);
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_process_array_append(ni_testbus_process_array_t *array, ni_testbus_process_t *process)
{
	array->data = ni_realloc(array->data, (array->count + 1) * sizeof(array->data[0]));
	array->data[array->count++] = ni_testbus_process_get(process);
}

