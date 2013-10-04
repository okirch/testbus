#include <dborb/dbus-model.h>
#include <testbus/model.h>

ni_dbus_class_t		ni_testbus_host_class = {
	.name		= NI_TESTBUS_HOST_CLASS,
};

ni_dbus_class_t		ni_testbus_hostlist_class = {
	.name		= NI_TESTBUS_HOSTLIST_CLASS,
};

static ni_dbus_method_t		ni_testbus_hostlist_methods[] = {
	{ "createHost",		"s",		},
	{ "reconnect",		"sab",		},
	{ NULL }
};

static ni_dbus_service_t	ni_testbus_hostlist_service = {
	.name		= NI_TESTBUS_HOSTLIST_INTERFACE,
	.compatible	= &ni_testbus_hostlist_class,
	.methods	= ni_testbus_hostlist_methods,
};

static ni_dbus_method_t		ni_testbus_host_methods[] = {
	{ NULL }
};

static ni_dbus_service_t	ni_testbus_host_service = {
	.name		= NI_TESTBUS_HOST_INTERFACE,
	.compatible	= &ni_testbus_host_class,
	.methods	= ni_testbus_host_methods,
};

void
ni_testbus_register_builtin_host(void)
{
	ni_objectmodel_register_class(&ni_testbus_hostlist_class);
	ni_objectmodel_register_service(&ni_testbus_hostlist_service);
	ni_objectmodel_register_class(&ni_testbus_host_class);
	ni_objectmodel_register_service(&ni_testbus_host_service);
}

