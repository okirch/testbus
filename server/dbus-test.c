
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

const char *
ni_testbus_testcase_full_path(const ni_dbus_object_t *container_object, const ni_testbus_testcase_t *test)
{
	static char pathbuf[256];

	snprintf(pathbuf, sizeof(pathbuf), "%s/Test/%u", container_object->path, test->id);
	return pathbuf;
}

ni_dbus_object_t *
ni_testbus_testcase_wrap(ni_dbus_object_t *container_object, ni_testbus_testcase_t *testcase)
{
	ni_dbus_object_t *object;

	object = ni_objectmodel_create_object(
			ni_dbus_object_get_server(container_object),
			ni_testbus_testcase_full_path(container_object, testcase),
			ni_testbus_testcase_class(),
			&testcase->context);

	ni_testbus_bind_container_interfaces(object, &testcase->context);
	return object;
}

ni_testbus_testcase_t *
ni_testbus_testcase_unwrap(const ni_dbus_object_t *object, DBusError *error)
{
	ni_testbus_container_t *context;
	ni_testbus_testcase_t *testcase;

	if (!ni_dbus_object_get_handle_typecheck(object, ni_testbus_testcase_class(), error))
		return NULL;

	if (!(context = ni_testbus_container_unwrap(object, error)))
		return NULL;

	testcase = ni_container_of(context, ni_testbus_testcase_t, context);
	ni_assert(context = &testcase->context);

	return testcase;
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
	int rc;

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
		ni_dbus_set_error_from_code(error, rc, "unable to create new test \"%s\"", name);
		return FALSE;
	}

	/* Register this object */
	test_object = ni_testbus_testcase_wrap(object, test);
	ni_dbus_message_append_string(reply, test_object->path);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Testset, createTest);


/*
 * Testcase.addHost(name, host-object-path)
 */
static dbus_bool_t
__ni_Testbus_Testcase_addHost(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_dbus_object_t *root_object, *host_object = NULL;
	ni_testbus_testcase_t *testcase;
	const char *name, *host_object_path;
	ni_testbus_host_t *host;

	testcase = ni_testbus_testcase_unwrap(object, error);
	if (testcase == NULL)
		return FALSE;

	if (argc != 2
	 || !ni_dbus_variant_get_string(&argv[0], &name)
	 || !ni_dbus_variant_get_string(&argv[1], &host_object_path))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if (!ni_testbus_identifier_valid(name, error))
		return FALSE;

	if (ni_testbus_container_get_host_by_role(&testcase->context, name) != NULL) {
		dbus_set_error(error, NI_DBUS_ERROR_NAME_EXISTS,
				"you already have a host by this role");
		return FALSE;
	}

	{
		ni_dbus_server_t *server = ni_dbus_object_get_server(object);

		root_object = NULL;
		if (server)
			root_object = ni_dbus_server_get_root_object(server);
		if (root_object)
			host_object = ni_dbus_object_lookup(root_object, host_object_path);
	}

	if (host_object == NULL) {
		dbus_set_error(error, NI_DBUS_ERROR_NAME_UNKNOWN,
				"unknown host object path %s", host_object_path);
		return FALSE;
	}

	if (!ni_dbus_object_isa(host_object, ni_testbus_host_class())) {
		dbus_set_error(error, NI_DBUS_ERROR_NOT_COMPATIBLE,
				"object %s is not a host object", host_object_path);
		return FALSE;
	}

	if (!(host = ni_testbus_host_unwrap(host_object, error)))
		return FALSE;

	if (!ni_testbus_host_set_role(host, name, &testcase->context)) {
		dbus_set_error(error, NI_DBUS_ERROR_IN_USE, "host already in use");
		return FALSE;
	}

	ni_testbus_container_add_host(&testcase->context, host);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Testcase, addHost);

static ni_dbus_property_t       __ni_Testbus_Testcase_properties[] = {
	NI_DBUS_GENERIC_STRING_PROPERTY(testbus_testcase, name, name, RO),
	{ NULL }
};
NI_TESTBUS_PROPERTIES_BINDING(Testcase);


void
ni_testbus_bind_builtin_test(void)
{
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Testset_createTest_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Testcase_addHost_binding);
	ni_dbus_objectmodel_bind_properties(&__ni_Testbus_Testcase_Properties_binding);
}


