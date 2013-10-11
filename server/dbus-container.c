
#include <ctype.h>
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/dbus-model.h>
#include <testbus/model.h>
#include <dborb/logging.h>

#include "model.h"
#include "container.h"
#include "host.h"

static struct ni_testbus_container_child_info {
	unsigned int		feature;
	const char *		service;
} ni_testbus_container_child_info[] = {
	{ NI_TESTBUS_CONTAINER_HAS_ENV,			NI_TESTBUS_ENVIRON_INTERFACE	},
	{ NI_TESTBUS_CONTAINER_HAS_CMDS,		NI_TESTBUS_CMDQUEUE_INTERFACE	},
	{ NI_TESTBUS_CONTAINER_HAS_FILES,		NI_TESTBUS_FILESET_INTERFACE	},
	{ NI_TESTBUS_CONTAINER_HAS_TESTS,		NI_TESTBUS_TESTSET_INTERFACE	},
	{ NI_TESTBUS_CONTAINER_HAS_HOSTS,		NI_TESTBUS_HOSTSET_INTERFACE	},
//	{ NI_TESTBUS_CONTAINER_HAS_PROCS,		NI_TESTBUS_PROCSET_INTERFACE	},
	{ 0 }
};

ni_testbus_container_t *
ni_testbus_container_unwrap(const ni_dbus_object_t *object, DBusError *error)
{
	ni_testbus_container_t *container;

	container = ni_dbus_object_get_handle_typecheck(object, ni_testbus_container_class(), error);
	return container;
}

void
ni_testbus_create_static_objects_container(ni_dbus_server_t *server)
{
	ni_dbus_object_t *object;

	/* Create a global object */
	object = ni_objectmodel_create_object(server, NI_TESTBUS_GLOBAL_CONTEXT_PATH, ni_testbus_container_class(), ni_testbus_global_context());
	ni_testbus_bind_container_interfaces(object, ni_testbus_global_context());
}

ni_bool_t
__ni_testbus_identifier_valid(const char *name)
{
	if (!isalpha(*name) && *name != '_')
		return FALSE;
	while (*++name) {
		if (!isalnum(*name) && *name != '_')
			return FALSE;
	}
	return TRUE;
}


ni_bool_t
ni_testbus_identifier_valid(const char *name, DBusError *error)
{
	if (__ni_testbus_identifier_valid(name))
		return TRUE;

	dbus_set_error(error, NI_DBUS_ERROR_NAME_INVALID, "invalid identifier \"%s\"", name);
	return FALSE;
}

void
ni_testbus_bind_container_interfaces(ni_dbus_object_t *object, ni_testbus_container_t *container)
{
	struct ni_testbus_container_child_info *info;

	ni_trace("%s()", __func__);
	if (!ni_dbus_object_isa(object, ni_testbus_container_class())) {
		ni_warn("bind_container_interfaces(%s): object's class \"%s\" is not derived from \"%s\"",
				object->path, object->class->name,
				NI_TESTBUS_CONTEXT_CLASS);
		return;
	}

	ni_trace("%s: bind container interfaces", object->path);
	for (info = ni_testbus_container_child_info; info->feature; ++info) {
		if (container->features & info->feature) {
			const ni_dbus_service_t *service;

			if ((service = ni_objectmodel_service_by_name(info->service)) == NULL) {
				ni_warn("cannot bind interface for container feature 0x%x: unknown dbus service %s",
						info->feature, info->service);
			} else {
				ni_trace("  -> %s", service->name);
				ni_dbus_object_register_service(object, service);
			}
		}
	}
}



/*
 * Container.getChildByName(class, name)
 */
static dbus_bool_t
__ni_Testbus_Container_getChildByName(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_container_t *container;
	const char *class_name, *child_name, *child_path = NULL;
	const ni_dbus_class_t *class;
	ni_dbus_object_t *child_object = NULL;

	if (!(container = ni_testbus_container_unwrap(object, error)))
		return FALSE;

	if (argc != 2
	 || !ni_dbus_variant_get_string(&argv[0], &class_name)
	 || !ni_dbus_variant_get_string(&argv[1], &child_name))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if (!(class = ni_objectmodel_get_class(class_name))) {
		dbus_set_error(error, NI_DBUS_ERROR_NAME_UNKNOWN, "unknown class name %s", class_name);
		return FALSE;
	}

	if (class == ni_testbus_file_class()) {
		ni_testbus_file_t *file;

		if ((file = ni_testbus_container_get_file_by_name(container, child_name)) != NULL)
			child_path = ni_testbus_file_full_path(object, file);
	}

	if (child_path == NULL) {
		dbus_set_error(error, NI_DBUS_ERROR_NAME_UNKNOWN, "no %s child named %s", class_name, child_name);
		return FALSE;
	}

	ni_debug_wicked("%s.%s(%s, %s) returns %s", object->path, method->name, class_name, child_name, child_path);
	ni_dbus_message_append_string(reply, child_path);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Container, getChildByName);

void
ni_testbus_bind_builtin_container(void)
{
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Container_getChildByName_binding);
}
