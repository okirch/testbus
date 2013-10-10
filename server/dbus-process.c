
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/logging.h>
#include <testbus/model.h>

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

static ni_dbus_property_t       __ni_Testbus_Process_properties[] = {
	//NI_DBUS_GENERIC_STRING_PROPERTY(testbus_command, name, name, RO),
	{ NULL }
};

NI_TESTBUS_PROPERTIES_BINDING(Process);

void
ni_testbus_bind_builtin_process(void)
{
//	ni_dbus_objectmodel_bind_method(&__ni_Testbus_ProcessQueue_kill_binding);
	ni_dbus_objectmodel_bind_properties(&__ni_Testbus_Process_Properties_binding);
}

