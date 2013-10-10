/*
 * DBus objectmodel
 *
 * Copyright (C) 2013 Olaf Kirch <okir@suse.de>
 */


#ifndef __WICKED_DBUS_ABSTRACT_MODEL_H__
#define __WICKED_DBUS_ABSTRACT_MODEL_H__

#include <dborb/dbus.h>

typedef struct ni_dbus_objectmodel	ni_dbus_objectmodel_t;

struct ni_dbus_objectmodel {
	const char *		bus_name;
	const char *		bus_name_prefix;

	const char *		root_object_path;
	const char *		root_interface_name;

	void			(*register_builtin)(void);
	void			(*bind_builtin)(void);
	void			(*create_static_objects)(ni_dbus_server_t *);
};

extern const ni_dbus_objectmodel_t *	ni_objectmodel_global;

extern ni_bool_t			ni_objectmodel_register(const ni_dbus_objectmodel_t *);
extern ni_xs_scope_t *			ni_objectmodel_get_schema(void);

extern void				ni_objectmodel_register_service(ni_dbus_service_t *service);
extern const ni_dbus_service_t *	ni_objectmodel_service_by_name(const char *name);
extern const ni_dbus_service_t *	ni_objectmodel_service_by_class(const ni_dbus_class_t *class);

extern void				ni_objectmodel_register_class(const ni_dbus_class_t *class);
extern ni_dbus_class_t *		ni_objectmodel_class_new(const char *, const ni_dbus_class_t *);
extern const ni_dbus_class_t *		ni_objectmodel_get_class(const char *name);

extern ni_dbus_server_t *		ni_objectmodel_create_server(void);
extern ni_dbus_object_t *		ni_objectmodel_create_object(ni_dbus_server_t *server,
						const char *object_path, const ni_dbus_class_t *object_class,
						void *object_handle);
extern ni_dbus_client_t *		ni_objectmodel_create_client(void);
extern dbus_bool_t			ni_objectmodel_bind_compatible_interfaces(ni_dbus_object_t *);

typedef struct ni_dbus_objectmodel_method_binding {
	const char *		service;
	const ni_dbus_method_t	method;
} ni_dbus_objectmodel_method_binding_t;

typedef struct ni_dbus_objectmodel_properties_binding {
	const char *		service;
	const ni_dbus_property_t *properties;
} ni_dbus_objectmodel_properties_binding_t;

extern ni_dbus_method_t *		ni_dbus_objectmodel_bind_method(const ni_dbus_objectmodel_method_binding_t *);
extern ni_bool_t			ni_dbus_objectmodel_bind_properties(const ni_dbus_objectmodel_properties_binding_t *);

extern ni_dbus_object_t *		ni_objectmodel_object_by_path(const char *path);

extern void				ni_objectmodel_register_event(ni_event_t, const char *);
extern dbus_bool_t			ni_objectmodel_event_send_signal(ni_dbus_server_t *, ni_event_t, const ni_uuid_t *);

#endif /* __WICKED_DBUS_ABSTRACT_MODEL_H__ */
