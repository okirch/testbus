/*
 * Simple DBus server functions
 *
 * Copyright (C) 2011-2012 Olaf Kirch <okir@suse.de>
 */


#ifndef __WICKED_DBUS_SERVER_H__
#define __WICKED_DBUS_SERVER_H__

#include <dbus/dbus.h>
#include "dbus-connection.h"

extern ni_dbus_server_t *	__ni_dbus_server_open(const char *bus_type, ni_bool_t private, const char *bus_name);
extern ni_dbus_object_t *	__ni_dbus_server_init_root(ni_dbus_server_t *, const char *, void *);
extern const char *		ni_dbus_server_request_name_prefix(ni_dbus_server_t *, const char *);

#endif /* __WICKED_DBUS_SERVER_H__ */

