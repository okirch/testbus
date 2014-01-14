/*
 * Handle global application config file
 *
 * Copyright (C) 2010-2014 Olaf Kirch <okir@suse.de>
 */


#ifndef __NI_NETINFO_APPCONFIG_H__
#define __NI_NETINFO_APPCONFIG_H__

#include <dborb/types.h>
#include <dborb/netinfo.h>
#include <dborb/logging.h>

struct ni_script_action {
	ni_script_action_t *	next;
	char *			name;
	ni_shellcmd_t *		process;
};

typedef struct ni_c_binding ni_c_binding_t;
struct ni_c_binding {
	ni_c_binding_t *	next;
	char *			name;
	char *			library;
	char *			symbol;
};

struct ni_extension {
	ni_extension_t *	next;

	/* Name of the extension; could be "dhcp4" or "ibft". */
	char *			name;

	/* Supported dbus interface */
	char *			interface;

	/* Shell commands */
	ni_script_action_t *	actions;

	/* C bindings */
	ni_c_binding_t *	c_bindings;

	/* Environment variables.
	 * The values are of the form
	 *   $object-path
	 *   $property:property-name
	 */
	ni_var_array_t		environment;
};

typedef struct ni_config_fslocation {
	char *			path;
	unsigned int		mode;
} ni_config_fslocation_t;

typedef struct ni_config {
	ni_config_fslocation_t	piddir;
	ni_config_fslocation_t	statedir;
	ni_config_fslocation_t	backupdir;
	unsigned int		recv_max;

	ni_extension_t *	dbus_extensions;
	ni_extension_t *	ns_extensions;

	char *			dbus_name;
	char *			dbus_type;
	char *			dbus_socket;
	char *			dbus_xml_schema_file;
} ni_config_t;

extern ni_config_t *	ni_config_new();
extern ni_config_t *	ni_config_parse(const char *, ni_init_appdata_callback_t *, void *);
extern ni_extension_t *	ni_config_find_extension(const char *);

extern ni_extension_t *	ni_extension_list_find(ni_extension_t *, const char *);
extern void		ni_extension_list_destroy(ni_extension_t **);
extern ni_extension_t *	ni_extension_new(ni_extension_t **, const char *);
extern void		ni_extension_free(ni_extension_t *);

extern void		ni_c_binding_free(ni_c_binding_t *);
extern void *		ni_c_binding_get_address(const ni_c_binding_t *);

extern ni_shellcmd_t *	ni_extension_script_new(ni_extension_t *, const char *name, const char *command);
extern ni_shellcmd_t *	ni_extension_script_find(const ni_extension_t *, const char *);
extern const ni_c_binding_t *ni_extension_find_c_binding(const ni_extension_t *, const char *name);

typedef struct ni_global {
	int			initialized;
	char *			config_path;
	ni_config_t *		config;

	void			(*other_event)(unsigned int);
} ni_global_t;

extern ni_global_t	ni_global;

static inline void
__ni_assert_initialized(void)
{
	if (!ni_global.initialized)
		ni_fatal("Library not initialized, please call ni_init() first");
}

static inline const char *
ni_config_dbus_xml_schema_file()
{
	__ni_assert_initialized();
	return ni_global.config->dbus_xml_schema_file;
}

static inline const char *
ni_config_dbus_socket_path(void)
{
	__ni_assert_initialized();
	return ni_global.config->dbus_socket;
}


#endif /* __NI_NETINFO_APPCONFIG_H__ */
