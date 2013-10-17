
#include <stdlib.h>
#include <dborb/logging.h>
#include <dborb/process.h>
#include <testbus/file.h>
#include "command.h"

static void			ni_testbus_command_destroy(ni_testbus_container_t *);
static void			ni_testbus_command_free(ni_testbus_container_t *);
static void			ni_testbus_command_release(ni_testbus_container_t *);
static void			ni_testbus_process_destroy(ni_testbus_container_t *);
static void			ni_testbus_process_free(ni_testbus_container_t *);
static void			ni_testbus_process_release(ni_testbus_container_t *);

static struct ni_testbus_container_ops ni_testbus_command_ops = {
	.features		= NI_TESTBUS_CONTAINER_HAS_ENV |
		    		  NI_TESTBUS_CONTAINER_HAS_FILES,
	.dbus_name_prefix	= "Command",

	.destroy		= ni_testbus_command_destroy,
	.free			= ni_testbus_command_free,
	.release		= ni_testbus_command_release,
};

static struct ni_testbus_container_ops ni_testbus_process_ops = {
	.features		= NI_TESTBUS_CONTAINER_HAS_ENV |
				  NI_TESTBUS_CONTAINER_HAS_FILES,

	.dbus_name_prefix	= "Process",

	.destroy		= ni_testbus_process_destroy,
	.free			= ni_testbus_process_free,
	.release		= ni_testbus_process_release,
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

	while (array->count) {
		ni_testbus_command_t *cmd = array->data[--(array->count)];

		cmd->context.parent = NULL;
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

int
ni_testbus_command_array_index(ni_testbus_command_array_t *array, const ni_testbus_command_t *command)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		if (array->data[i] == command)
			return i;
	}
	return -1;
}

ni_testbus_command_t *
ni_testbus_command_array_take_at(ni_testbus_command_array_t *array, unsigned int index)
{
	ni_testbus_command_t *taken;

	if (index >= array->count)
		return NULL;

	taken = array->data[index];

	memmove(&array->data[index], &array->data[index+1], array->count - (index + 1));
	array->count --;

	return taken;
}

ni_bool_t
ni_testbus_command_array_remove(ni_testbus_command_array_t *array, const ni_testbus_command_t *command)
{
	ni_testbus_command_t *taken;
	int index;

	if ((index = ni_testbus_command_array_index(array, command)) < 0
	 || (taken = ni_testbus_command_array_take_at(array, index)) == NULL)
		return FALSE;

	/* Drop the reference to the command */
	ni_testbus_command_put(taken);
	return TRUE;
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
 * When the container owning the process goes away, we should also nuke
 * the command and its state.
 */
void
ni_testbus_command_release(ni_testbus_container_t *container)
{
	ni_debug_wicked("%s(%s)", __func__, container->dbus_object_path);
	ni_testbus_container_destroy(container);
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
	ni_testbus_container_set_owner(&proc->context, &command->context);

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

/*
 * When the Command owning the process goes away, we should also nuke
 * the process and its state.
 */
void
ni_testbus_process_release(ni_testbus_container_t *container)
{
	ni_debug_wicked("%s(%s)", __func__, container->dbus_object_path);
	ni_testbus_container_destroy(container);
}

void
ni_testbus_process_apply_context(ni_testbus_process_t *proc, ni_testbus_container_t *container)
{
	ni_testbus_container_merge_environment(container, &proc->context.env);
	ni_testbus_container_merge_files(container, &proc->context.files);
}

/*
 * Prior to starting the process, set up all the output files etc.
 */
static ni_bool_t
__ni_testbus_process_attach_stdio(ni_testbus_process_t *proc, const char *name)
{
	ni_testbus_file_t *file, *ofile;

	if ((file = __ni_testbus_container_get_file_by_name(&proc->context, name)) != NULL) {
		ni_testbus_file_get(file);
		ni_testbus_file_array_remove(&proc->context.files, file);
		ofile = ni_testbus_file_new(file->name, &proc->context.files);
		/* ofile->executable = file->executable; */
		ofile->type = NI_TESTBUS_FILE_WRITE;
		ni_testbus_file_put(file);
	}

	return TRUE;
}

ni_bool_t
ni_testbus_process_finalize(ni_testbus_process_t *proc)
{
	__ni_testbus_process_attach_stdio(proc, "stdout");
	__ni_testbus_process_attach_stdio(proc, "stderr");

	return TRUE;
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
	ni_testbus_process_array_t temp;
	unsigned int i;

	temp = *array;
	memset(array, 0, sizeof(*array));

	while (temp.count) {
		ni_testbus_process_t *cmd = temp.data[--(temp.count)];

		cmd->context.parent = NULL;
		ni_testbus_process_put(cmd);
	}
	free(temp.data);
}

void
ni_testbus_process_array_append(ni_testbus_process_array_t *array, ni_testbus_process_t *process)
{
	array->data = ni_realloc(array->data, (array->count + 1) * sizeof(array->data[0]));
	array->data[array->count++] = ni_testbus_process_get(process);
}

int
ni_testbus_process_array_index(ni_testbus_process_array_t *array, const ni_testbus_process_t *process)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		if (array->data[i] == process)
			return i;
	}
	return -1;
}

ni_testbus_process_t *
ni_testbus_process_array_take_at(ni_testbus_process_array_t *array, unsigned int index)
{
	ni_testbus_process_t *taken;

	if (index >= array->count)
		return NULL;

	taken = array->data[index];

	memmove(&array->data[index], &array->data[index+1], array->count - (index + 1));
	array->count --;

	return taken;
}

ni_bool_t
ni_testbus_process_array_remove(ni_testbus_process_array_t *array, const ni_testbus_process_t *process)
{
	ni_testbus_process_t *taken;
	int index;

	if ((index = ni_testbus_process_array_index(array, process)) < 0
	 || (taken = ni_testbus_process_array_take_at(array, index)) == NULL)
		return FALSE;

	/* Drop the reference to the process */
	ni_testbus_process_put(taken);
	return TRUE;
}

