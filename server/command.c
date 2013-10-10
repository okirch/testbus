
#include <stdlib.h>
#include <dborb/logging.h>
#include <dborb/process.h>
#include "command.h"

static unsigned int		__ni_testbus_command_next_id;
static unsigned int		__ni_testbus_process_next_id;

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
ni_testbus_command_get(ni_testbus_command_t *command)
{
	ni_assert(command->refcount);
	command->refcount += 1;
	return command;
}

void
ni_testbus_command_put(ni_testbus_command_t *command)
{
	ni_assert(command->refcount);
	if (--(command->refcount) == 0)
		ni_testbus_command_free(command);
}

ni_testbus_command_t *
ni_testbus_command_new(ni_testbus_container_t *parent, const ni_string_array_t *argv)
{
	ni_testbus_command_t *cmd;

	ni_assert(parent);
	ni_assert(ni_testbus_container_has_commands(parent));

	cmd = ni_malloc(sizeof(*cmd));
	cmd->refcount = 1;
	ni_string_array_copy(&cmd->argv, argv);
	cmd->id = __ni_testbus_process_next_id++;

	ni_testbus_container_init(&cmd->context,
			NI_TESTBUS_CONTAINER_HAS_ENV |
			NI_TESTBUS_CONTAINER_HAS_FILES,
			parent);

	ni_testbus_command_array_append(&parent->commands, cmd);

	/* Now unref the command */
	cmd->refcount--;

	return cmd;
}

void
ni_testbus_command_free(ni_testbus_command_t *cmd)
{
	ni_assert(cmd->refcount == 0);
	ni_string_array_destroy(&cmd->argv);
	ni_testbus_container_destroy(&cmd->context);
	free(cmd);
}

/*
 * Process handling
 * A process is a command in execution. There is no 1:1 relationship, as a command
 * may be executed on several hosts simultanously.
 */
ni_testbus_process_t *
ni_testbus_process_get(ni_testbus_process_t *process)
{
	ni_assert(process->refcount);
	process->refcount += 1;
	return process;
}

void
ni_testbus_process_put(ni_testbus_process_t *process)
{
	ni_assert(process->refcount);
	if (--(process->refcount) == 0)
		ni_testbus_process_free(process);
}

ni_testbus_process_t *
ni_testbus_process_new(ni_testbus_container_t *parent, ni_testbus_command_t *command)
{
	ni_testbus_process_t *proc;

	ni_assert(parent);
	ni_assert(command);
	ni_assert(ni_testbus_container_has_processes(parent));

	proc = ni_malloc(sizeof(*proc));
	proc->refcount = 1;
	proc->command = ni_testbus_command_get(command);
	ni_string_array_copy(&proc->argv, &command->argv);
	proc->id = __ni_testbus_command_next_id++;

	ni_testbus_container_init(&proc->context,
			NI_TESTBUS_CONTAINER_HAS_ENV |
			NI_TESTBUS_CONTAINER_HAS_FILES,
			parent);

	ni_testbus_process_array_append(&parent->processes, proc);

	/* Now unref the command */
	proc->refcount--;

	return proc;
}

void
ni_testbus_process_free(ni_testbus_process_t *proc)
{
	ni_assert(proc->refcount == 0);
	ni_string_array_destroy(&proc->argv);
	ni_testbus_container_destroy(&proc->context);
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
static void
__ni_testbus_process_notify(ni_process_t *proc)
{
}

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

