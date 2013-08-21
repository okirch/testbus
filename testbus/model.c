
#include <dborb/dbus-model.h>
#include <testbus/model.h>

ni_dbus_objectmodel_t	ni_testbus_objectmodel = {
	.bus_name		= NI_TESTBUS_DBUS_BUS_NAME,
	.root_object_path	= NI_TESTBUS_ROOT_PATH,
	.root_interface_name	= NI_TESTBUS_ROOT_INTERFACE,
};
