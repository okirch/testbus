
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

	snprintf(pathbuf, sizeof(pathbuf), "%s/Process/%u", container_object->path, process->id);
	return pathbuf;
}

ni_dbus_object_t *
ni_testbus_process_wrap(ni_dbus_object_t *container_object, ni_testbus_process_t *process)
{
	ni_dbus_object_t *object;

	object = ni_objectmodel_create_object(
			ni_dbus_object_get_server(container_object),
			ni_testbus_process_full_path(container_object, process),
			ni_testbus_process_class(),
			&process->context);

	ni_testbus_bind_container_interfaces(object, &process->context);
	return object;
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

	if ((file = ni_testbus_container_get_file_by_name(&proc->context, "stdout")) != NULL) {
		ni_trace("stdout has %u bytes of data", file->size);
		exit_info->stdout_bytes = file->size;
	}
	if ((file = ni_testbus_container_get_file_by_name(&proc->context, "stderr")) != NULL) {
		ni_trace("stderr has %u bytes of data", file->size);
		exit_info->stderr_bytes = file->size;
	}

#ifdef notyet
	ni_process_set_exit_info(proc, exit_info);
#endif

	ni_testbus_process_exit_info_serialize(exit_info, &dict);

	/* Now just re-broadcast the exit_info to everyone who is interested */
	ni_dbus_server_send_signal(ni_dbus_object_get_server(object), object,
			NI_TESTBUS_PROCESS_INTERFACE,
			"processExited",
			1, &dict);
	ni_dbus_variant_destroy(&dict);

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


static ni_dbus_property_t       __ni_Testbus_Process_properties[] = {
	//NI_DBUS_GENERIC_STRING_PROPERTY(testbus_command, name, name, RO),
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

