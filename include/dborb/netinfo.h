/*
 * Global header file for netinfo library
 *
 * Copyright (C) 2009-2012 Olaf Kirch <okir@suse.de>
 */

#ifndef __WICKED_NETINFO_H__
#define __WICKED_NETINFO_H__

#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>

#include <dborb/types.h>
#include <dborb/constants.h>
#include <dborb/util.h>

extern void		ni_set_global_config_path(const char *);
extern int		ni_init(const char *appname);
typedef ni_bool_t	ni_init_appdata_callback_t(void *, const xml_node_t *);
extern int		ni_init_ex(const char *appname, ni_init_appdata_callback_t *, void *);

extern int		ni_server_background(const char *);
extern void		ni_server_listen_other_events(void (*handler)(ni_event_t));
extern ni_xs_scope_t *	ni_server_dbus_xml_schema(void);
extern const char *	ni_config_statedir(void);
extern const char *	ni_config_backupdir(void);

extern ni_dbus_client_t *ni_create_dbus_client(const char *bus_name);

extern ni_netconfig_t * ni_netconfig_new(void);
extern void		ni_netconfig_free(ni_netconfig_t *);
extern void		ni_netconfig_init(ni_netconfig_t *);
extern void		ni_netconfig_destroy(ni_netconfig_t *);

extern ni_netconfig_t *	ni_global_state_handle(int);

extern void		ni_sockaddr_set_ipv4(ni_sockaddr_t *, struct in_addr, uint16_t);
extern void		ni_sockaddr_set_ipv6(ni_sockaddr_t *, struct in6_addr, uint16_t);
extern ni_opaque_t *	ni_sockaddr_pack(const ni_sockaddr_t *, ni_opaque_t *);
extern ni_sockaddr_t *	ni_sockaddr_unpack(ni_sockaddr_t *, const ni_opaque_t *);
extern ni_opaque_t *	ni_sockaddr_prefix_pack(const ni_sockaddr_t *, unsigned int, ni_opaque_t *);
extern ni_sockaddr_t *	ni_sockaddr_prefix_unpack(ni_sockaddr_t *, unsigned int *, const ni_opaque_t *);

extern const char *	ni_print_link_flags(int flags);
extern const char *	ni_print_link_type(int type);
extern const char *	ni_print_integer_nice(unsigned long long, const char *);

extern const char *	ni_strerror(int errcode);


#endif /* __WICKED_NETINFO_H__ */
