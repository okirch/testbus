
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>

#include "model.h"
#include "container.h"

ni_testbus_env_t *
ni_testbus_environ_unwrap(const ni_dbus_object_t *object, DBusError *error)
{
	ni_testbus_container_t *context;

	if ((context = ni_testbus_container_unwrap(object, error)) == NULL)
		return FALSE;

	return &context->env;
}

void *
ni_objectmodel_get_testbus_environ(const ni_dbus_object_t *object, ni_bool_t write_access, DBusError *error)
{
	return ni_testbus_environ_unwrap(object, error);
}

/*
 * Environment.setenv(name, value)
 */
static dbus_bool_t
__ni_Testbus_Environment_setenv(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_env_t *env;
	const char *name, *value;

	if (argc != 2
	 || !ni_dbus_variant_get_string(&argv[0], &name)
	 || !ni_dbus_variant_get_string(&argv[1], &value)
	 || !ni_testbus_env_name_valid(name)
	 || value == NULL)
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if (ni_testbus_env_name_reserved(name)) {
		dbus_set_error(error, NI_DBUS_ERROR_NAME_INVALID,
				"Cannot set reserved environment variable name \"%s\"",
				name);
		return FALSE;
	}

	if (!(env = ni_testbus_environ_unwrap(object, error)))
		return FALSE;

	ni_testbus_setenv(env, name, value);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Environment, setenv);

void
ni_testbus_bind_builtin_environment(void)
{
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Environment_setenv_binding);
}
