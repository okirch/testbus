
#ifndef __SERVER_MODEL_P_H__
#define __SERVER_MODEL_P_H__

#include <dborb/dbus-model.h>
#include <testbus/model.h>
#include "types.h"


#define NI_TESTBUS_CONTROLLER_PATH	NI_TESTBUS_OBJECT_ROOT "/Controller"
#define NI_TESTBUS_CONTROLLER_CMD_PATH	NI_TESTBUS_OBJECT_ROOT "/Controller/Command"
#define NI_TESTBUS_CONTROLLER_QUEUE_PATH NI_TESTBUS_OBJECT_ROOT "/Controller/Queue"
#define NI_TESTBUS_CONTROLLER_ENV_PATH	NI_TESTBUS_OBJECT_ROOT "/Controller/DefaultEnvironment"


/*
 * Controller
 * Methods:
 *  createCommand()
 *
 * Children:
 *  DefaultEnvironment	interface: Environment
 *  Queue		interface: CommandQueue
 */
#define NI_TESTBUS_CONTROLLER_INTERFACE	NI_TESTBUS_NAMESPACE ".Controller"

extern ni_bool_t	ni_testbus_identifier_valid(const char *name, DBusError *);

extern void		ni_testbus_bind_container_interfaces(ni_dbus_object_t *, ni_testbus_container_t *);

extern void		ni_testbus_create_static_objects_container(ni_dbus_server_t *);
extern void		ni_testbus_bind_builtin_command(void);
extern void		ni_testbus_create_static_objects_host(ni_dbus_server_t *);
extern void		ni_testbus_bind_builtin_environ(void);
extern void		ni_testbus_create_static_objects_environ(ni_dbus_server_t *);
extern void		ni_testbus_create_static_objects_container(ni_dbus_server_t *);
extern void		ni_testbus_bind_builtin_host(void);
extern void		ni_testbus_bind_builtin_file(void);
extern void		ni_testbus_create_static_objects_file(ni_dbus_server_t *server);
extern void		ni_testbus_bind_builtin_test(void);
extern void		ni_testbus_create_static_objects_test(ni_dbus_server_t *server);
extern void		ni_testbus_bind_builtin_process(void);

extern void		ni_testbus_record_wellknown_bus_name(const char *, const char *);
extern const char *	ni_testbus_lookup_wellknown_bus_name(const char *);

ni_dbus_object_t *	ni_testbus_host_wrap(ni_dbus_server_t *server, ni_testbus_host_t *host);
ni_testbus_host_t *	ni_testbus_host_unwrap(const ni_dbus_object_t *object, DBusError *error);
ni_dbus_object_t *	ni_testbus_testcase_wrap(ni_dbus_object_t *container_object, ni_testbus_testcase_t *testcase);
ni_testbus_testcase_t *	ni_testbus_testcase_unwrap(const ni_dbus_object_t *object, DBusError *error);
ni_dbus_object_t *	ni_testbus_command_wrap(ni_dbus_object_t *container_object, ni_testbus_command_t *command);
ni_testbus_command_t *	ni_testbus_command_unwrap(const ni_dbus_object_t *object, DBusError *error);
ni_dbus_object_t *	ni_testbus_process_wrap(ni_dbus_object_t *container_object, ni_testbus_process_t *process);
ni_testbus_process_t *	ni_testbus_process_unwrap(const ni_dbus_object_t *object, DBusError *error);
ni_dbus_object_t *	ni_testbus_environ_wrap(const ni_dbus_object_t *parent, ni_testbus_env_t *env);

ni_testbus_container_t *ni_testbus_container_unwrap(const ni_dbus_object_t *, DBusError *);

#endif /* __SERVER_MODEL_P_H__ */
