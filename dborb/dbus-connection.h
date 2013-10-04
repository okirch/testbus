/*
 * Simple DBus connection handling functions
 *
 * Copyright (C) 2011-2012 Olaf Kirch <okir@suse.de>
 */


#ifndef __WICKED_DBUS_CONNECTION_H__
#define __WICKED_DBUS_CONNECTION_H__

#include <dbus/dbus.h>
#include "dbus-common.h"

extern ni_dbus_connection_t *	ni_dbus_connection_open(const char *bus_type, const char *bus_name);
extern void			ni_dbus_connection_free(ni_dbus_connection_t *);
extern dbus_bool_t		ni_dbus_connection_request_name(ni_dbus_connection_t *connection, const char *bus_name);
extern const char *		ni_dbus_connection_request_name_index(ni_dbus_connection_t *, const char *);
extern const char *		ni_dbus_connection_get_local_bus_name(const ni_dbus_connection_t *connection);
extern ni_dbus_connection_t *	ni_dbus_connection_ref(ni_dbus_connection_t *);
extern ni_dbus_connection_t *	__ni_dbus_connection_open(const char *bus_type_string, ni_bool_t private, const char *bus_name);

extern ni_dbus_message_t *	ni_dbus_connection_call(ni_dbus_connection_t *connection,
					ni_dbus_message_t *call, unsigned int call_timeout, DBusError *error);
extern int			ni_dbus_connection_call_async(ni_dbus_connection_t *connection,
					ni_dbus_message_t *call, unsigned int timeout,
					ni_dbus_async_callback_t *callback, ni_dbus_object_t *proxy);
extern int			ni_dbus_connection_send_message(ni_dbus_connection_t *, ni_dbus_message_t *);
extern void			ni_dbus_connection_send_error(ni_dbus_connection_t *, ni_dbus_message_t *, DBusError *);
extern void			ni_dbus_add_signal_handler(ni_dbus_connection_t *conn,
					const char *sender,
					const char *object_path,
					const char *object_interface,
					ni_dbus_signal_handler_t *callback,
					void *user_data);
extern void			ni_dbus_connection_register_object(ni_dbus_connection_t *, ni_dbus_object_t *);
extern void			ni_dbus_connection_unregister_object(ni_dbus_connection_t *, ni_dbus_object_t *);
extern int			ni_dbus_async_server_call_run_command(ni_dbus_connection_t *conn,
					ni_dbus_object_t *object,
					const ni_dbus_method_t *method,
					DBusMessage *call_message,
					ni_process_t *process);
extern void			ni_dbus_mainloop(ni_dbus_connection_t *);

extern int			ni_dbus_connection_get_caller_uid(ni_dbus_connection_t *, const char *, uid_t *);

extern ni_dbus_client_t *	ni_dbus_client_create(ni_dbus_connection_t *, const char *remote_bus_name);
extern const char *		ni_dbus_client_get_remote_bus_name(const ni_dbus_client_t *);
extern const char *		ni_dbus_client_get_local_bus_name(const ni_dbus_client_t *);
extern const char *		ni_dbus_server_get_local_bus_name(const ni_dbus_server_t *);

#endif /* __WICKED_DBUS_CONNECTION_H__ */
