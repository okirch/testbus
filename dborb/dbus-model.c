/*
 * DBus generic interfaces for wicked
 *
 * Copyright (C) 2011-2012 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/poll.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>

#include <dborb/netinfo.h>
#include <dborb/logging.h>
#include <dborb/xml.h>
#include <dborb/dbus-errors.h>
#include <dborb/dbus-model.h>
#include <dborb/dbus-service.h>
#include "util_priv.h"
#include "dbus-common.h"
#include "xml-schema.h"
#include "appconfig.h"
#include "debug.h"
#include "dbus-connection.h"
#include "dbus-server.h"

extern ni_dbus_object_t *	ni_objectmodel_new_interface(ni_dbus_server_t *server,
					const ni_dbus_service_t *service,
					const ni_dbus_variant_t *dict, DBusError *error);

#define NI_DBUS_SERVICES_MAX	128
typedef struct ni_dbus_service_array {
	unsigned int		count;
	const ni_dbus_service_t *services[NI_DBUS_SERVICES_MAX];
} ni_dbus_service_array_t;

#define NI_DBUS_CLASSES_MAX	1024
typedef struct ni_dbus_class_array {
	unsigned int		count;
	const ni_dbus_class_t *	class[NI_DBUS_CLASSES_MAX];
} ni_dbus_class_array_t;

static ni_dbus_class_array_t	ni_objectmodel_class_registry;
static ni_dbus_service_array_t	ni_objectmodel_service_registry;

const ni_dbus_objectmodel_t *	ni_objectmodel_global = NULL;
ni_dbus_server_t *		__ni_objectmodel_server;
ni_xs_scope_t *			__ni_objectmodel_schema;

static ni_xs_scope_t *		ni_objectmodel_init_schema(void);
static int			ni_objectmodel_bind_extensions(void);

/*
 * Register an objectmodel
 */
ni_bool_t
ni_objectmodel_register(const ni_dbus_objectmodel_t *model)
{
	ni_assert(ni_objectmodel_global == NULL);

	ni_assert(model->bus_name || model->root_object_path);
	ni_assert(model->root_interface_name);
	ni_objectmodel_global = model;

	__ni_objectmodel_schema = ni_objectmodel_init_schema();
	if (__ni_objectmodel_schema == NULL)
		ni_fatal("Giving up.");

	/* Register all built-in classes, notations and services */
	if (model->register_builtin)
		model->register_builtin();

	/* Register/amend all services defined in the schema */
	ni_dbus_xml_register_services(__ni_objectmodel_schema);

	if (model->bind_builtin)
		model->bind_builtin();

	/* Register dynamic naming services */
	ni_dbus_register_dynamic_lookups();

	/* Bind all extensions */
	ni_objectmodel_bind_extensions();

	return TRUE;
}

ni_xs_scope_t *
ni_objectmodel_get_schema(void)
{
	ni_assert(__ni_objectmodel_schema);
	return __ni_objectmodel_schema;
}

/*
 * Create the dbus service
 */
ni_dbus_server_t *
ni_objectmodel_create_server(void)
{
	ni_dbus_server_t *server;
	ni_dbus_object_t *object;

	ni_assert(ni_objectmodel_global != NULL);

	if (ni_objectmodel_global->bus_name_prefix == NULL) {
		server = ni_dbus_server_open(NULL, ni_objectmodel_global->bus_name, NULL);
		if (server == NULL)
			goto failed;

		object = ni_dbus_server_get_root_object(server);
	} else {
		const char *bus_name;

		server = __ni_dbus_server_open(NULL, FALSE, NULL);
		if (server == NULL)
			goto failed;

		bus_name = ni_dbus_server_request_name_prefix(server, ni_objectmodel_global->bus_name_prefix);
		if (bus_name == NULL)
			goto failed;
		ni_debug_dbus("acquired bus name \"%s\"", bus_name);

		object = __ni_dbus_server_init_root(server, ni_objectmodel_global->root_object_path, NULL);
	}

	/* Register root interface with the root of the object hierarchy */
	if (ni_objectmodel_global->root_interface_name) {
		const ni_dbus_service_t *service;

		service = ni_objectmodel_service_by_name(ni_objectmodel_global->root_interface_name);
		if (service == NULL)
			ni_fatal("Object model specifies unknown root interface \"%s\"",
					ni_objectmodel_global->root_interface_name);

		ni_dbus_object_register_service(object, service);
	}
	ni_debug_testbus("create server handle, bus_name=%s, root object=%s (class %s, interface %s)",
			ni_dbus_server_get_local_bus_name(server),
			object->path, object->class? object->class->name : NULL,
			ni_objectmodel_global->root_interface_name);

	if (ni_objectmodel_global->create_static_objects)
		ni_objectmodel_global->create_static_objects(server);

	__ni_objectmodel_server = server;
	return server;

failed:
	ni_fatal("unable to initialize dbus service");
}

/*
 * Create a client for the given objectmodel
 */
ni_dbus_client_t *
ni_objectmodel_create_client(void)
{
	ni_dbus_client_t *client;

	ni_assert(ni_objectmodel_global != NULL);

	/* FIXME CRAP */
	if (ni_objectmodel_global->bus_name == NULL) {
		ni_error("%s: cannot create client, bus_name is NULL", __func__);
		return NULL;
	}

	client = ni_dbus_client_open(NULL, ni_objectmodel_global->bus_name);
	if (client && ni_objectmodel_global->root_object_path) {
		const ni_dbus_class_t *class = &ni_dbus_anonymous_class;
		ni_dbus_object_t *object;

#if 0
		if (ni_objectmodel_global->root_class_name
		 && !(class = ni_objectmodel_get_class(ni_objectmodel_global->root_class_name)))
			ni_fatal("objectmodel specifies unknown root class \"%s\"", ni_objectmodel_global->root_class_name);
#endif

		object = ni_dbus_client_object_new(client, class,
				ni_objectmodel_global->root_object_path,
				NULL, NULL);
		ni_dbus_client_set_root_object(client, object);
	}

	return client;
}

ni_xs_scope_t *
ni_objectmodel_init_schema(void)
{
	const char *filename = ni_global.config->dbus_xml_schema_file;
	ni_xs_scope_t *scope;

	if (filename == NULL) {
		ni_error("Cannot create dbus xml schema: no schema path configured");
		return NULL;
	}

	scope = ni_dbus_xml_init();
	if (ni_xs_process_schema_file(filename, scope) < 0) {
		ni_error("Cannot create dbus xml schema: error in schema definition");
		ni_xs_scope_free(scope);
		return NULL;
	}

	return scope;
}

/*
 * Create an object as an instance of a specific class.
 * This will automatically bind all interfaces compatible with this
 * class.
 */
ni_dbus_object_t *
ni_objectmodel_create_object(ni_dbus_server_t *server,
		const char *object_path, const ni_dbus_class_t *object_class,
		void *object_handle)
{
	ni_dbus_object_t *object;

	object = ni_dbus_server_register_object(server, object_path, object_class, object_handle);
	if (object == NULL)
		ni_fatal("Unable to create dbus object for %s", object_path);

	ni_objectmodel_bind_compatible_interfaces(object);
	return object;
}

/*
 * Create the initial object hierarchy
 */
dbus_bool_t
ni_objectmodel_bind_compatible_interfaces(ni_dbus_object_t *object)
{
	unsigned int i;

	if (object->class == NULL) {
		ni_error("%s: object \"%s\" without class", __func__, object->path);
		return FALSE;
	}

	NI_TRACE_ENTER_ARGS("object=%s, class=%s", object->path, object->class->name);
	for (i = 0; i < ni_objectmodel_service_registry.count; ++i) {
		const ni_dbus_service_t *service = ni_objectmodel_service_registry.services[i];

		/* If the service is compatible with the object's dbus class,
		 * or any of its superclasses, register this interface to this
		 * object */
		if (ni_dbus_object_isa(object, service->compatible))
			ni_dbus_object_register_service(object, service);
	}

	return TRUE;
}

unsigned int
ni_objectmodel_compatible_services_for_class(const ni_dbus_class_t *query_class,
		const ni_dbus_service_t **list, unsigned int max)
{
	unsigned int i, count;

	for (i = count = 0; i < ni_objectmodel_service_registry.count; ++i) {
		const ni_dbus_service_t *service = ni_objectmodel_service_registry.services[i];
		const ni_dbus_class_t *class;

		/* If the service is compatible with the object's dbus class,
		 * or any of its superclasses, register this interface to this
		 * object */
		for (class = query_class; class; class = class->superclass) {
			if (service->compatible == class) {
				if (count < max)
					list[count++] = service;
				break;
			}
		}
	}

	return count;
}

/*
 * objectmodel service registry
 */
static ni_dbus_method_t *
__ni_objectmodel_service_clone_methods(const ni_dbus_method_t *array)
{
	ni_dbus_method_t *new_array;
	unsigned int count;

	if (array == NULL)
		return NULL;

	for (count = 0; array[count].name; ++count)
		;

	new_array = ni_calloc(count + 1, sizeof(new_array[0]));
	memcpy(new_array, array, count * sizeof(new_array[0]));
	return new_array;
}

static ni_dbus_property_t *
__ni_objectmodel_service_clone_properties(const ni_dbus_property_t *array)
{
	ni_dbus_property_t *new_array;
	unsigned int count;

	if (array == NULL)
		return NULL;

	for (count = 0; array[count].name; ++count)
		;

	new_array = ni_calloc(count + 1, sizeof(new_array[0]));
	memcpy(new_array, array, count * sizeof(new_array[0]));
	return new_array;
}

void
ni_objectmodel_register_service(ni_dbus_service_t *service)
{
	unsigned int index = ni_objectmodel_service_registry.count;

	ni_assert(index < NI_DBUS_SERVICES_MAX);

	service->methods = __ni_objectmodel_service_clone_methods(service->methods);
	service->signals = __ni_objectmodel_service_clone_methods(service->signals);
	service->properties = __ni_objectmodel_service_clone_properties(service->properties);

	ni_objectmodel_service_registry.services[index++] = service;
	ni_objectmodel_service_registry.count = index;
}

ni_dbus_property_t *
ni_objectmodel_service_register_property(ni_dbus_service_t *service, const char *name, const char *signature)
{
	ni_dbus_property_t *array, *property;
	unsigned int count = 0;

	if ((array = (ni_dbus_property_t *) service->properties) != NULL) {
		for (count = 0; service->properties[count].name; ++count)
			;
	}

	service->properties = array = ni_realloc(array, (count + 2) * sizeof(ni_dbus_property_t));

	property = &array[count];
	memset(property, 0, 2 * sizeof(*property));

	ni_string_dup((char **) &property->name, name);
	ni_string_dup((char **) &property->signature, signature);

	return property;
}

const ni_dbus_service_t *
ni_objectmodel_service_by_name(const char *name)
{
	unsigned int i;

	for (i = 0; i < ni_objectmodel_service_registry.count; ++i) {
		const ni_dbus_service_t *service = ni_objectmodel_service_registry.services[i];

		if (!strcmp(service->name, name))
			return service;
	}

	return NULL;
}

const ni_dbus_service_t *
ni_objectmodel_service_by_class(const ni_dbus_class_t *class)
{
	unsigned int i;

	for (i = 0; i < ni_objectmodel_service_registry.count; ++i) {
		const ni_dbus_service_t *service = ni_objectmodel_service_registry.services[i];

		if (service->compatible == class)
			return service;
	}

	return NULL;
}

const ni_dbus_service_t *
ni_objectmodel_service_by_tag(const char *tag)
{
	unsigned int i;

	for (i = 0; i < ni_objectmodel_service_registry.count; ++i) {
		const ni_dbus_service_t *service = ni_objectmodel_service_registry.services[i];
		const ni_xs_service_t *xs_service;

		if ((xs_service = service->schema) != NULL
		 && ni_string_eq(xs_service->name, tag))
			return service;
	}

	return NULL;
}

const ni_dbus_service_t *
ni_objectmodel_factory_service(const ni_dbus_service_t *service)
{
	const ni_xs_service_t *xs_service;
	const char *factory_name = NULL;
	char namebuf[256];

	/* See if the schema specifies a factory service explicitly */
	if ((xs_service = service->schema) != NULL) {
		const ni_var_t *attr;

		attr = ni_var_array_get(&xs_service->attributes, "factory");
		if (attr)
			factory_name = attr->value;
	}
	
	/* If not, the default is to append ".Factory" to the service name */
	if (factory_name == NULL) {
		snprintf(namebuf, sizeof(namebuf), "%s.Factory", service->name);
		factory_name = namebuf;
	}

	return ni_objectmodel_service_by_name(factory_name);
}

const ni_dbus_service_t *
ni_objectmodel_auth_service(const ni_dbus_service_t *service)
{
	const ni_xs_service_t *xs_service;
	const char *auth_name = NULL;
	char namebuf[256];

	/* See if the schema specifies a auth service explicitly */
	if ((xs_service = service->schema) != NULL) {
		const ni_var_t *attr;

		attr = ni_var_array_get(&xs_service->attributes, "auth");
		if (attr)
			auth_name = attr->value;
	}
	
	/* If not, the default is to append ".Auth" to the service name */
	if (auth_name == NULL) {
		snprintf(namebuf, sizeof(namebuf), "%s.Auth", service->name);
		auth_name = namebuf;
	}

	return ni_objectmodel_service_by_name(auth_name);
}

/*
 * objectmodel service registry
 * This is mostly needed for doing proper type checking when binding
 * extensions
 */
void
ni_objectmodel_register_class(const ni_dbus_class_t *class)
{
	unsigned int index = ni_objectmodel_class_registry.count;

	ni_assert(class->name);
	ni_assert(index < NI_DBUS_CLASSES_MAX);

	ni_objectmodel_class_registry.class[index++] = class;
	ni_objectmodel_class_registry.count = index;
}

const ni_dbus_class_t *
ni_objectmodel_get_class(const char *name)
{
	unsigned int i;

	for (i = 0; i < ni_objectmodel_class_registry.count; ++i) {
		const ni_dbus_class_t *class = ni_objectmodel_class_registry.class[i];

		if (!strcmp(class->name, name))
			return class;
	}
	return NULL;
}

ni_dbus_class_t *
ni_objectmodel_class_new(const char *classname, const ni_dbus_class_t *base_class)
{
	ni_dbus_class_t *new_class;

	/* Create the new class */
	new_class = xcalloc(1, sizeof(*new_class));
	ni_string_dup(&new_class->name, classname);
	new_class->superclass = base_class;

	/* inherit all methods from netif */
	if (base_class) {
		new_class->list = base_class->list;
		new_class->destroy = base_class->destroy;
		new_class->refresh = base_class->refresh;
	}

	return new_class;
}

/*
 * Do method binding
 */
static ni_bool_t
__ni_dbus_method_bind(ni_dbus_method_t *method, const ni_dbus_method_t *binding)
{
	if (binding->call_signature != NULL) {
		if (method->call_signature == NULL)
			ni_string_dup((char **) &method->call_signature, binding->call_signature);
		else
		if (!ni_string_eq(method->call_signature, binding->call_signature))
			return FALSE;
	}

	method->handler = binding->handler;
	method->handler_ex = binding->handler_ex;
	return TRUE;
}

ni_dbus_method_t *
ni_dbus_objectmodel_bind_method(const ni_dbus_objectmodel_method_binding_t *b)
{
	const ni_dbus_service_t *service;
	const ni_dbus_method_t *method;

	service = ni_objectmodel_service_by_name(b->service);
	if (service == NULL) {
		ni_error("Unable to bind method %s.%s: no such interface",
				b->service, b->method.name);
		return NULL;
	}

	method = ni_dbus_service_get_method(service, b->method.name);
	if (!method) {
		ni_error("Unable to bind method %s.%s: no such method",
				b->service, b->method.name);
		return NULL;
	}

	if (!__ni_dbus_method_bind((ni_dbus_method_t *) method, &b->method)) {
		ni_error("Unable to bind method %s.%s: signature mismatch",
				b->service, b->method.name);
		return NULL;
	}

	ni_debug_dbus("successfully bound %s.%s", service->name, method->name);
	return (ni_dbus_method_t *) method;
}

/*
 * Do property binding
 */
static ni_bool_t
__ni_dbus_property_bind(ni_dbus_property_t *property, const ni_dbus_property_t *binding)
{
	if (binding->signature != NULL) {
		if (property->signature == NULL)
			ni_string_dup((char **) &property->signature, binding->signature);
		else
		if (!ni_string_eq(property->signature, binding->signature))
			return FALSE;
	}

	property->get = binding->get;
	property->set = binding->set;
	property->parse = binding->parse;
	property->update = binding->update;
	property->generic = binding->generic;

	return TRUE;
}

ni_bool_t
ni_dbus_objectmodel_bind_properties(const ni_dbus_objectmodel_properties_binding_t *b)
{
	const ni_dbus_service_t *service;
	const ni_dbus_property_t *bp;

	service = ni_objectmodel_service_by_name(b->service);
	if (service == NULL) {
		ni_error("Unable to bind properties for %s: no such interface", b->service);
		return FALSE;
	}

	for (bp = b->properties; bp->name; ++bp) {
		const ni_dbus_property_t *property;

		property = ni_dbus_service_get_property(service, bp->name);
		if (property == NULL) {
			ni_debug_dbus("register property %s.%s", service->name, bp->name);
			property = ni_objectmodel_service_register_property((ni_dbus_service_t *) service,
							bp->name, bp->signature);
		}

		ni_debug_dbus("binding property %s.%s", service->name, bp->name);
		if (!__ni_dbus_property_bind((ni_dbus_property_t *) property, bp)) {
			ni_error("Unable to bind property %s.%s: signature mismatch",
					b->service, bp->name);
			return FALSE;
		}
	}

	return TRUE;
}

/*
 * Do an object lookup using the full path
 */
ni_dbus_object_t *
ni_objectmodel_object_by_path(const char *path)
{
	ni_dbus_object_t *root;

	if (!__ni_objectmodel_server || !path)
		return NULL;

	root = ni_dbus_server_get_root_object(__ni_objectmodel_server);
	if (path[0] == '/' && !(path = ni_dbus_object_get_relative_path(root, path)))
		return NULL;

	return ni_dbus_object_lookup(root, path);
}

/*
 * Handle event->signal mapping
 */
static struct {
	unsigned int	count;
	ni_intmap_t *	map;
} __ni_objectmodel_signals;

void
ni_objectmodel_register_event(unsigned int event, const char *signal_name)
{
	unsigned int size = (__ni_objectmodel_signals.count + 2) * sizeof(__ni_objectmodel_signals.map[0]);
	ni_intmap_t *entry;

	__ni_objectmodel_signals.map = xrealloc(__ni_objectmodel_signals.map, size);

	entry = &__ni_objectmodel_signals.map[__ni_objectmodel_signals.count++];
	entry->name = xstrdup(signal_name);
	entry->value = event;
	entry++;

	memset(entry, 0, sizeof(*entry));
}

const char *
ni_objectmodel_event_to_signal(unsigned int event)
{
	ni_intmap_t *map;

	if ((map = __ni_objectmodel_signals.map) == NULL)
		return NULL;
	return ni_format_uint_mapped(event, __ni_objectmodel_signals.map);
}

/*
 * Send out an event (not associated with a network device or other object)
 */
dbus_bool_t
ni_objectmodel_event_send_signal(ni_dbus_server_t *server, unsigned int event, const ni_uuid_t *uuid)
{
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	const char *signal_name = NULL;
	unsigned int argc = 0;

	if (!(signal_name = ni_objectmodel_event_to_signal(event)))
		return FALSE;

	if (!server && !(server = __ni_objectmodel_server)) {
		ni_error("%s: help! No dbus server handle! Cannot send signal.", __func__);
		return FALSE;
	}

	if (uuid) {
		ni_dbus_variant_set_uuid(&arg, uuid);
		argc++;
	}

	ni_debug_dbus("sending event \"%s\"", signal_name);
	ni_dbus_server_send_signal(server,
				ni_dbus_server_get_root_object(server),
				ni_objectmodel_global->root_interface_name,
				signal_name, argc, &arg);

	ni_dbus_variant_destroy(&arg);
	return TRUE;
}

/*
 * Bind extension scripts to the interface functions they are specified for.
 */
int
ni_objectmodel_bind_extensions(void)
{
	unsigned int i;

	NI_TRACE_ENTER();
	for (i = 0; i < ni_objectmodel_service_registry.count; ++i) {
		const ni_dbus_service_t *service = ni_objectmodel_service_registry.services[i];
		ni_extension_t *extension;

		extension = ni_config_find_extension(service->name);
		if (extension != NULL)
			ni_dbus_service_bind_extension(service, extension);
	}

	return 0;
}

