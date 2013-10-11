
#ifndef __NI_TESTBUS_CLIENT_H__
#define __NI_TESTBUS_CLIENT_H__

#include <testbus/types.h>

extern void			ni_call_init_client(ni_dbus_client_t *client);
extern ni_dbus_object_t *	ni_testbus_call_get_object(const char *path);
extern ni_dbus_object_t *	ni_testbus_call_get_and_refresh_object(const char *path);
extern ni_dbus_object_t *	ni_testbus_call_get_container(const char *path);
extern ni_dbus_object_t *	ni_testbus_call_create_host(const char *name);
extern ni_dbus_object_t *	ni_testbus_call_create_test(const char *name, ni_dbus_object_t *parent);
extern ni_dbus_object_t *	ni_testbus_call_create_tempfile(const char *name, ni_dbus_object_t *parent);
extern ni_dbus_object_t *	ni_testbus_call_reconnect_host(const char *, const ni_uuid_t *);
extern ni_bool_t		ni_testbus_call_remove_host(const char *name);
extern ni_dbus_object_t *	ni_testbus_call_claim_host_by_name(const char *, ni_dbus_object_t *, const char *);
extern ni_dbus_object_t *	ni_testbus_call_claim_host_by_capability(const char *, ni_dbus_object_t *, const char *);
extern ni_dbus_object_t *	ni_testbus_call_get_agent(const char *);
extern ni_buffer_t *		ni_testbus_agent_retrieve_file(ni_dbus_object_t *, const char *);
extern ni_bool_t		ni_testbus_call_upload_file(ni_dbus_object_t *, const ni_buffer_t *);
extern ni_bool_t		ni_testbus_agent_add_capability(ni_dbus_object_t *, const char *);
extern ni_bool_t		ni_testbus_agent_add_capabilities(ni_dbus_object_t *, const ni_string_array_t *);
extern ni_dbus_object_t *	ni_testbus_call_create_command(ni_dbus_object_t *, const ni_string_array_t *);
extern ni_dbus_object_t *	ni_testbus_call_host_run(ni_dbus_object_t *, const ni_dbus_object_t *);
extern ni_bool_t		ni_testbus_wait_for_process(const ni_dbus_object_t *, long, ni_testbus_process_exit_status_t *);
extern ni_bool_t		ni_testbus_call_process_exit(ni_dbus_object_t *, const ni_testbus_process_exit_status_t *);

#endif /* __NI_TESTBUS_CLIENT_H__ */
