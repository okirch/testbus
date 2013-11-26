
#ifndef __NI_TESTBUS_MODEL_H__
#define __NI_TESTBUS_MODEL_H__

#include <dborb/dbus.h>
#include <dborb/dbus-model.h>
#include <testbus/protocol.h>

extern const ni_dbus_service_t	ni_testbus_root_interface;
extern ni_dbus_objectmodel_t	ni_testbus_objectmodel;


#ifndef __NI_STR
# define __NI_STR(x)	#x
#endif

#define __NI_TESTBUS_METHOD_BINDING(Interface, Method, InterfaceName) \
ni_dbus_objectmodel_method_binding_t __ni_Testbus_##Interface##_##Method##_binding = { \
	.service = InterfaceName, \
	.method = { \
		.name = __NI_STR(Method), \
		.handler = __ni_Testbus_##Interface##_##Method, \
	} \
}
#define NI_TESTBUS_METHOD_BINDING(Interface, Method) \
	__NI_TESTBUS_METHOD_BINDING(Interface, Method, NI_TESTBUS_NAMESPACE "." #Interface)

#define __NI_TESTBUS_ASYNC_METHOD_BINDING(Interface, Method, InterfaceName) \
ni_dbus_objectmodel_method_binding_t __ni_Testbus_##Interface##_##Method##_binding = { \
	.service = InterfaceName, \
	.method = { \
		.name = __NI_STR(Method), \
		.async_handler = __ni_Testbus_##Interface##_##Method##_AsyncCall, \
		.async_completion = __ni_Testbus_##Interface##_##Method##_AsyncCompletion, \
	} \
}
#define NI_TESTBUS_ASYNC_METHOD_BINDING(Interface, Method) \
	__NI_TESTBUS_ASYNC_METHOD_BINDING(Interface, Method, NI_TESTBUS_NAMESPACE "." #Interface)

#define __NI_TESTBUS_EXT_METHOD_BINDING(Interface, Method, InterfaceName) \
ni_dbus_objectmodel_method_binding_t __ni_Testbus_##Interface##_##Method##_binding = { \
	.service = InterfaceName, \
	.method = { \
		.name = __NI_STR(Method), \
		.handler_ex = __ni_Testbus_##Interface##_##Method##_ex, \
	} \
}
#define NI_TESTBUS_EXT_METHOD_BINDING(Interface, Method) \
	__NI_TESTBUS_EXT_METHOD_BINDING(Interface, Method, NI_TESTBUS_NAMESPACE "." #Interface)

#define NI_TESTBUS_PROPERTIES_BINDING(Interface) \
ni_dbus_objectmodel_properties_binding_t __ni_Testbus_##Interface##_Properties_binding = { \
	.service = NI_TESTBUS_NAMESPACE "." __NI_STR(Interface), \
	.properties = __ni_Testbus_##Interface##_properties, \
}

extern const ni_dbus_class_t *	ni_testbus_hostlist_class(void);
extern const ni_dbus_class_t *	ni_testbus_container_class(void);
extern const ni_dbus_class_t *	ni_testbus_host_class(void);
extern const ni_dbus_class_t *	ni_testbus_command_class(void);
extern const ni_dbus_class_t *	ni_testbus_process_class(void);
extern const ni_dbus_class_t *	ni_testbus_command_queue_class(void);
extern const ni_dbus_class_t *	ni_testbus_agent_class(void);
extern const ni_dbus_class_t *	ni_testbus_filesystem_class(void);
extern const ni_dbus_class_t *	ni_testbus_fileset_class(void);
extern const ni_dbus_class_t *	ni_testbus_file_class(void);
extern const ni_dbus_class_t *	ni_testbus_testset_class(void);
extern const ni_dbus_class_t *	ni_testbus_testcase_class(void);

extern const ni_dbus_service_t *ni_testbus_host_interface(void);
extern const ni_dbus_service_t *ni_testbus_eventlog_interface(void);

#endif /* __NI_TESTBUS_MODEL_H__ */

