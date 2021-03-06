
#ifndef __NI_TESTBUS_CLIENT_H__
#define __NI_TESTBUS_CLIENT_H__

#include <testbus/types.h>
#include <dborb/dbus.h>

typedef struct ni_testbus_client_timeout ni_testbus_client_timeout_t;
struct ni_testbus_client_timeout {
	unsigned int		timeout_msec;
	int			(*busy_wait)(const ni_testbus_client_timeout_t *);
	void			(*timedout)(const ni_testbus_client_timeout_t *);
	void *			user_data;

	const void *		handle;

	unsigned int		num_busywaits;
};

typedef struct ni_testus_client_host_state {
	ni_dbus_object_t *	host_object;
	uint32_t		host_gen;

	ni_bool_t		ready;
} ni_testus_client_host_state_t;

extern void			ni_testbus_client_init(ni_dbus_client_t *client);
extern ni_dbus_object_t *	ni_testbus_client_get_object(const char *path);
extern ni_dbus_object_t *	ni_testbus_client_get_and_refresh_object(const char *path);
extern ni_dbus_object_t *	ni_testbus_client_get_container(const char *path);
extern ni_dbus_object_t *	ni_testbus_client_container_child_by_name(ni_dbus_object_t *, const ni_dbus_class_t *, const char *);
extern ni_dbus_object_t *	ni_testbus_client_create_host(const char *name);
extern ni_dbus_object_t *	ni_testbus_client_create_test(const char *name, ni_dbus_object_t *parent);
extern ni_dbus_object_t *	ni_testbus_client_create_tempfile(const char *name, unsigned int mode, ni_dbus_object_t *parent);
extern ni_dbus_object_t *	ni_testbus_client_reconnect_host(const char *, ni_uuid_t *);
extern ni_bool_t		ni_testbus_client_remove_host(const char *name);
extern ni_bool_t		ni_testbus_client_delete(ni_dbus_object_t *);
extern ni_dbus_object_t *	ni_testbus_client_claim_host_by_name(const char *, ni_dbus_object_t *, const char *);
extern ni_dbus_object_t *	ni_testbus_client_claim_host_by_capability(const char *, ni_dbus_object_t *, const char *,
						ni_testbus_client_timeout_t *);
extern ni_dbus_object_t *	ni_testbus_client_get_agent(const char *);
extern ni_bool_t		ni_testbus_client_setenv(ni_dbus_object_t *, const char *name, const char *value);
extern char *			ni_testbus_client_getenv(ni_dbus_object_t *, const char *name);
extern ni_bool_t		ni_testbus_client_eventlog_append(ni_dbus_object_t *, const ni_event_t *);
extern ni_bool_t		ni_testbus_client_eventlog_purge(ni_dbus_object_t *, unsigned int until_seq);
extern ni_buffer_t *		ni_testbus_client_agent_download_file(ni_dbus_object_t *, const char *);
extern ni_bool_t		ni_testbus_client_agent_upload_file(ni_dbus_object_t *, const char *, const ni_buffer_t *);
extern ni_bool_t		ni_testbus_client_upload_file(ni_dbus_object_t *, const ni_buffer_t *);
extern ni_buffer_t *		ni_testbus_client_download_file(ni_dbus_object_t *);
extern ni_bool_t		ni_testbus_agent_add_capability(ni_dbus_object_t *, const char *);
extern ni_bool_t		ni_testbus_agent_add_capabilities(ni_dbus_object_t *, const ni_string_array_t *);
extern ni_bool_t		ni_testbus_agent_add_environment(ni_dbus_object_t *, const ni_var_array_t *);
extern ni_dbus_object_t *	ni_testbus_client_create_command(ni_dbus_object_t *, const ni_string_array_t *, ni_bool_t use_terminal);
extern ni_bool_t		ni_testbus_client_command_add_file(ni_dbus_object_t *, const char *, const ni_buffer_t *, unsigned int);
extern ni_dbus_object_t *	ni_testbus_client_host_run(ni_dbus_object_t *, const ni_dbus_object_t *);
extern ni_bool_t		ni_testbus_client_host_shutdown(ni_dbus_object_t *, ni_bool_t reboot_flag, ni_testus_client_host_state_t *state);
extern ni_bool_t		ni_testbus_client_host_wait_for_reboot(unsigned int nhosts,
						ni_testus_client_host_state_t *hosts,
						ni_testbus_client_timeout_t *);
extern ni_bool_t		ni_testbus_wait_for_process(ni_dbus_object_t *, long, ni_process_exit_info_t *);
extern ni_bool_t		ni_testbus_client_process_exit(ni_dbus_object_t *, const ni_process_exit_info_t *);

extern void			ni_testbus_client_timeout_init(ni_testbus_client_timeout_t *, unsigned int msec);

#endif /* __NI_TESTBUS_CLIENT_H__ */
