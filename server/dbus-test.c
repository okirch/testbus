
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/logging.h>

#include "model.h"
#include "fileset.h"
#include "host.h"
#include "testcase.h"
#include "container.h"

void
ni_testbus_create_static_objects_test(ni_dbus_server_t *server)
{
}

ni_dbus_object_t *
ni_testbus_testcase_wrap(ni_dbus_object_t *parent_object, ni_testbus_testcase_t *testcase)
{
	return ni_testbus_container_wrap(parent_object, ni_testbus_testcase_class(), &testcase->context);
}

ni_testbus_testcase_t *
ni_testbus_testcase_unwrap(const ni_dbus_object_t *object, DBusError *error)
{
	ni_testbus_container_t *context;

	if (!ni_dbus_object_get_handle_typecheck(object, ni_testbus_testcase_class(), error))
		return NULL;

	if (!(context = ni_testbus_container_unwrap(object, error)))
		return NULL;

	return ni_testbus_testcase_cast(context);
}

void *
ni_objectmodel_get_testbus_testcase(const ni_dbus_object_t *object, ni_bool_t write_access, DBusError *error)
{
	return ni_testbus_testcase_unwrap(object, error);
}

/*
 * Testset.createTest(name)
 *
 */
static dbus_bool_t
__ni_Testbus_Testset_createTest(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_container_t *context;
	ni_dbus_object_t *test_object;
	ni_testbus_testcase_t *test;
	const char *name;

	if ((context = ni_testbus_container_unwrap(object, error)) == NULL)
		return FALSE;

	if (argc != 1 || !ni_dbus_variant_get_string(&argv[0], &name))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if (!ni_testbus_identifier_valid(name, error))
		return FALSE;

	if (ni_testbus_container_get_test_by_name(context, name) != NULL) {
		dbus_set_error(error, NI_DBUS_ERROR_NAME_EXISTS, "test case with this name already exists");
		return FALSE;
	}

	if ((test = ni_testbus_testcase_new(name, context)) == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "unable to create new test \"%s\"", name);
		return FALSE;
	}

	/* Register this object */
	test_object = ni_testbus_testcase_wrap(object, test);
	ni_dbus_message_append_string(reply, test_object->path);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Testset, createTest);


static ni_dbus_property_t       __ni_Testbus_Testcase_properties[] = {
	NI_DBUS_GENERIC_STRING_PROPERTY(testbus_testcase, name, context.name, RO),
	{ NULL }
};
NI_TESTBUS_PROPERTIES_BINDING(Testcase);


void
ni_testbus_bind_builtin_test(void)
{
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Testset_createTest_binding);
	ni_dbus_objectmodel_bind_properties(&__ni_Testbus_Testcase_Properties_binding);
}


