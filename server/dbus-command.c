
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/logging.h>
#include <testbus/model.h>

#include "model.h"
#include "command.h"

const char *
ni_testbus_command_full_path(const ni_dbus_object_t *container_object, const ni_testbus_command_t *command)
{
	static char pathbuf[256];

	snprintf(pathbuf, sizeof(pathbuf), "%s/Command%u", container_object->path, command->context.id);
	return pathbuf;
}

ni_dbus_object_t *
ni_testbus_command_wrap(ni_dbus_object_t *parent_object, ni_testbus_command_t *command)
{
	return ni_testbus_container_wrap(parent_object, ni_testbus_command_class(), &command->context);
}

ni_testbus_command_t *
ni_testbus_command_unwrap(const ni_dbus_object_t *object, DBusError *error)
{
	ni_testbus_container_t *context;
	ni_testbus_command_t *command;

	if (!ni_dbus_object_get_handle_typecheck(object, ni_testbus_command_class(), error))
		return NULL;

	if (!(context = ni_testbus_container_unwrap(object, error)))
		return NULL;

	return ni_testbus_command_cast(context);
}

void *
ni_objectmodel_get_testbus_command(const ni_dbus_object_t *object, ni_bool_t write_access, DBusError *error)
{
	return ni_testbus_command_unwrap(object, error);
}

/*
 * CommandQueue.createCommand(argv[])
 *
 */
static dbus_bool_t
__ni_Testbus_CommandQueue_createCommand(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_container_t *context;
	ni_string_array_t cmd_argv = NI_STRING_ARRAY_INIT;
	ni_dbus_object_t *command_object;
	ni_testbus_command_t *command;
	const char *name;
	int rc;

	if ((context = ni_testbus_container_unwrap(object, error)) == NULL)
		return FALSE;

	if (argc != 1
	 || !ni_dbus_variant_get_string_array(&argv[0], &cmd_argv))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	/* Create the new command and place it on the queue */
	command = ni_testbus_command_new(context, &cmd_argv);
	ni_string_array_destroy(&cmd_argv);

	if (command == NULL) {
		ni_dbus_set_error_from_code(error, rc, "unable to create new command \"%s\"", name);
		return FALSE;
	}

	/* Register this object */
	command_object = ni_testbus_command_wrap(object, command);
	ni_testbus_container_set_owner(&command->context, context);

	ni_dbus_message_append_string(reply, command_object->path);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(CommandQueue, createCommand);

static ni_dbus_property_t       __ni_Testbus_Command_properties[] = {
	//NI_DBUS_GENERIC_STRING_PROPERTY(testbus_command, name, name, RO),
	{ NULL }
};

NI_TESTBUS_PROPERTIES_BINDING(Command);

void
ni_testbus_bind_builtin_command(void)
{
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_CommandQueue_createCommand_binding);
	ni_dbus_objectmodel_bind_properties(&__ni_Testbus_Command_Properties_binding);
}
