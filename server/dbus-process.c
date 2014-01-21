
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/logging.h>
#include <dborb/process.h>
#include <testbus/model.h>
#include <testbus/process.h>
#include <testbus/file.h>

#include "model.h"
#include "command.h"

const char *
ni_testbus_process_full_path(const ni_dbus_object_t *container_object, const ni_testbus_process_t *process)
{
	static char pathbuf[256];

	snprintf(pathbuf, sizeof(pathbuf), "%s/Process%u", container_object->path, process->context.id);
	return pathbuf;
}

ni_dbus_object_t *
ni_testbus_process_wrap(ni_dbus_object_t *parent_object, ni_testbus_process_t *process)
{
	return ni_testbus_container_wrap(parent_object, ni_testbus_process_class(), &process->context);
}

ni_testbus_process_t *
ni_testbus_process_unwrap(const ni_dbus_object_t *object, DBusError *error)
{
	ni_testbus_container_t *context;
	ni_testbus_process_t *process;

	if (!ni_dbus_object_get_handle_typecheck(object, ni_testbus_process_class(), error))
		return NULL;

	if (!(context = ni_testbus_container_unwrap(object, error)))
		return NULL;

	process = ni_container_of(context, ni_testbus_process_t, context);
	ni_assert(context = &process->context);

	return process;
}

void *
ni_objectmodel_get_testbus_process(const ni_dbus_object_t *object, ni_bool_t write_access, DBusError *error)
{
	return ni_testbus_process_unwrap(object, error);
}

/*
 * Process.setExitInfo(dict)
 */
static dbus_bool_t
__ni_Testbus_Process_setExitInfo(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_process_t *proc;
	ni_process_exit_info_t *exit_info;
	ni_testbus_file_t *file;
	ni_dbus_variant_t dict = NI_DBUS_VARIANT_INIT;

	if (!(proc = ni_testbus_process_unwrap(object, error)))
		return FALSE;

	if (argc != 1
	 || !(exit_info = ni_testbus_process_exit_info_deserialize(&argv[0])))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if ((file = ni_testbus_container_get_file_by_name(&proc->context, "stdout")) != NULL)
		exit_info->stdout_bytes = file->size;
	if ((file = ni_testbus_container_get_file_by_name(&proc->context, "stderr")) != NULL)
		exit_info->stderr_bytes = file->size;

	if (ni_debug & NI_TRACE_TESTBUS) {
		switch (exit_info->how) {
		case NI_PROCESS_NONSTARTER:
			ni_trace("%s.setExitInfo(NonStarter)", object->path);
			break;
		case NI_PROCESS_EXITED:
			ni_trace("%s.setExitInfo(Exited, status=%d)", object->path, exit_info->exit.code);
			break;
		case NI_PROCESS_CRASHED:
			ni_trace("%s.setExitInfo(Crashed, signal=%d)", object->path, exit_info->crash.signal);
			break;
		case NI_PROCESS_TIMED_OUT:
			ni_trace("%s.setExitInfo(TimedOut)", object->path);
			break;
		case NI_PROCESS_TRANSCENDED:
			ni_trace("%s.setExitInfo(Transcended)", object->path);
			break;
		default:
			ni_trace("%s.setExitInfo(Unknown=%d)", object->path, exit_info->how);
			break;
		}
	}

	/* Save the exit info */
	if (proc->process)
		ni_process_set_exit_info(proc->process, exit_info);

	/* Now just re-broadcast the exit_info to everyone who is interested */
	ni_testbus_process_exit_info_serialize(exit_info, &dict);
	ni_dbus_server_send_signal(ni_dbus_object_get_server(object), object,
			NI_TESTBUS_PROCESS_INTERFACE,
			"processExited",
			1, &dict);
	ni_dbus_variant_destroy(&dict);

	free(exit_info);
	return TRUE;
}

static dbus_bool_t
__ni_Testbus_Process_setExitInfo_ex(ni_dbus_object_t *object, const ni_dbus_method_call_ctx_t *ctx,
		ni_dbus_message_t *reply, DBusError *error)
{
	return __ni_Testbus_Process_setExitInfo(object,
			ctx->method, ctx->argc, ctx->argv,
			reply, error);
}

NI_TESTBUS_EXT_METHOD_BINDING(Process, setExitInfo);

/*
 * Helper function to get/set the exit-info property
 */
static dbus_bool_t
__ni_testbus_process_get_exit_info(const ni_dbus_object_t *object, const ni_dbus_property_t *property, ni_dbus_variant_t *result, DBusError *error)
{
	ni_testbus_process_t *proc;

	if (!(proc = ni_testbus_process_unwrap(object, error)))
		return FALSE;

	if (!proc->process || proc->process->exit_info.how == -1)
		return ni_dbus_error_property_not_present(error, object->path, property->name);

	return ni_testbus_process_exit_info_serialize(&proc->process->exit_info, result);
}

static dbus_bool_t
__ni_testbus_process_set_exit_info(ni_dbus_object_t *object, const ni_dbus_property_t *property, const ni_dbus_variant_t *value, DBusError *error)
{
	ni_testbus_process_t *proc;

	if (!(proc = ni_testbus_process_unwrap(object, error)))
		return FALSE;

	dbus_set_error(error, DBUS_ERROR_FAILED, "cannot set property %s - not supported", property->name);
	return FALSE;
}

static ni_dbus_property_t       __ni_Testbus_Process_properties[] = {
	//NI_DBUS_GENERIC_STRING_PROPERTY(testbus_command, name, name, RO),
	{
		.name = "exit-info",
		.signature = NI_DBUS_DICT_SIGNATURE,
		__NI_DBUS_PROPERTY_RO(__ni_testbus_process, exit_info),
	},
	{ NULL }
};

NI_TESTBUS_PROPERTIES_BINDING(Process);

void
ni_testbus_bind_builtin_process(void)
{
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Process_setExitInfo_binding);
//	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Process_kill_binding);
	ni_dbus_objectmodel_bind_properties(&__ni_Testbus_Process_Properties_binding);
}

