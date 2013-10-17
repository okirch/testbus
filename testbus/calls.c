/*
 * No REST for the wicked!
 *
 * Client-side functions for calling the wicked server.
 *
 * Copyright (C) 2010-2012 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <dborb/netinfo.h>
#include <dborb/logging.h>
#include <dborb/xml.h>
#include <dborb/buffer.h>
#include <testbus/model.h>
#include <testbus/client.h>
#include <testbus/process.h>
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/dbus-model.h>
#include <dborb/socket.h>
#include <dborb/process.h>

/*
 * Error context - this is an opaque type.
 */
typedef struct ni_call_error_context ni_call_error_context_t;
struct ni_call_error_context {
	void *handler;
	xml_node_t *		config;
	xml_node_t *		__allocated;

#define MAX_TRACKED_ERRORS	6
	struct ni_call_error_counter {
		unsigned int	count;
		char *		error_name;
		char *		error_message;
	} tracked[MAX_TRACKED_ERRORS];
};
#define NI_CALL_ERROR_CONTEXT_INIT(func, node) \
		{ .handler = func, .config = node, .__allocated = NULL }

#if 0
static void	ni_call_error_context_destroy(ni_call_error_context_t *);
#endif

/*
 * Local statics
 */
static ni_dbus_client_t *	ni_call_client;
static ni_dbus_object_t *	ni_call_root_object;

/*
 * Create the client
 */
void
ni_call_init_client(ni_dbus_client_t *client)
{
	ni_assert(ni_call_root_object == NULL);
	ni_assert(ni_call_client == NULL);

	if (client == NULL) {
		client = ni_objectmodel_create_client();
		if (!client)
			ni_fatal("Unable to connect to dbus service");
	}
	ni_call_client = client;
	ni_call_root_object = ni_dbus_client_get_root_object(client);
}


/*
 * Create the client and return the handle of the root object
 */
static ni_dbus_object_t *
ni_call_get_root(void)
{
	ni_assert(ni_call_root_object);
	return ni_call_root_object;
}

/*
 * Obtain an object handle, generic version
 */
static ni_dbus_object_t *
__ni_call_get_proxy_object(const ni_dbus_service_t *service, const char *relative_path)
{
	ni_dbus_object_t *root_object, *child;

	if (!(root_object = ni_call_get_root()))
		return NULL;

	child = ni_dbus_object_create(root_object, relative_path,
				service? service->compatible : NULL,
				NULL);

	if (service)
		ni_dbus_object_set_default_interface(child, service->name);

	return child;
}

/*
 * Obtain an object handle by name
 */
ni_dbus_object_t *
__ni_call_proxy_object_by_path(const char *path, const char *service_name)
{
	const ni_dbus_service_t *service = NULL;
	const char *relative_path;
	ni_dbus_object_t *object;

	relative_path = ni_string_strip_prefix(NI_TESTBUS_ROOT_PATH "/", path);
	if (relative_path == NULL) {
		ni_error("%s: object path \"%s\" does not start with %s",
				__func__, path,
				NI_TESTBUS_ROOT_PATH "/");
		return NULL;
	}

	if (service_name) {
		service = ni_objectmodel_service_by_name(service_name);
		ni_assert(service);
	}

	object = __ni_call_get_proxy_object(service, relative_path);

	ni_dbus_object_set_default_interface(object, service_name);
	return object;
}

enum {
	NI_TESTBUS_GET_NOUPDATE		= 0,
	NI_TESTBUS_GET_INTERFACE	= 1,
	NI_TESTBUS_GET_DATA		= 2,
};

static ni_dbus_object_t *
__ni_testbus_call_get_object(ni_dbus_object_t *root_object, const char *path, unsigned int how)
{
	ni_dbus_object_t *result;

	if (!root_object && !(root_object = ni_call_get_root()))
		return NULL;

	result = ni_dbus_object_create(root_object, path, NULL, NULL);
	if (!result)
		return NULL;

	switch (how) {
	case NI_TESTBUS_GET_INTERFACE:
#ifdef notyet
		if (!ni_dbus_object_introspect(result)) {
			ni_error("unable to retrieve interface info for object %s", path);
			return NULL;
		}
#else
		if (!ni_dbus_object_refresh_children(result)) {
			ni_error("unable to refresh object %s", path);
			return NULL;
		}
#endif
		break;

	case NI_TESTBUS_GET_DATA:
		/* Call ObjectManager.GetManagedObjects to get list of objects and their properties */
		if (!ni_dbus_object_refresh_children(result)) {
			ni_error("unable to refresh object %s", path);
			return NULL;
		}
		break;
	}

	return result;
}


ni_dbus_object_t *
ni_testbus_call_get_object(const char *path)
{
	return __ni_testbus_call_get_object(NULL, path, NI_TESTBUS_GET_NOUPDATE);
}

ni_dbus_object_t *
ni_testbus_call_get_and_refresh_object(const char *path)
{
	return __ni_testbus_call_get_object(NULL, path, NI_TESTBUS_GET_DATA);
}

ni_dbus_object_t *
ni_testbus_call_get_object_and_metadata(const char *path)
{
	return __ni_testbus_call_get_object(NULL, path, NI_TESTBUS_GET_INTERFACE);
}

ni_dbus_object_t *
ni_testbus_call_get_container(const char *path)
{
	ni_dbus_object_t *object;

	object = ni_testbus_call_get_object_and_metadata(path);
	if (object == NULL)
		return NULL;

	if (!ni_dbus_object_isa(object, ni_testbus_container_class())) {
		ni_error("DBus object %s is not a container object (class %s)",
				object->path,
				object->class->name);
		return NULL;
	}
	return object;
}

static ni_dbus_object_t *
__ni_testbus_handle_path_result(const ni_dbus_variant_t *res, const char *method_name)
{
	const char *value;

	if (!ni_dbus_variant_get_string(res, &value)) {
		ni_error("failed to decode %s() response", method_name);
		return NULL;
	}

	if (!value || !*value) {
		ni_error("%s() returns empty string", method_name);
		return NULL;
	}
	return ni_testbus_call_get_and_refresh_object(value);
}

static ni_dbus_object_t *
__ni_testbus_container_create_child(ni_dbus_object_t *container, const char *method_name, const char *name)
{
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_object_t *result = NULL;

	ni_assert(container);

	ni_dbus_variant_set_string(&arg, name);
	if (!ni_dbus_object_call_variant(container, NULL, method_name, 1, &arg, 1, &res, &error)) {
		ni_dbus_print_error(&error, "%s.%s(%s): failed", container->path, method_name, name);
		dbus_error_free(&error);
		goto failed;
	} else {
		result = __ni_testbus_handle_path_result(&res, method_name);
	}

failed:
	ni_dbus_variant_destroy(&arg);
	ni_dbus_variant_destroy(&res);
	return result;
}

static ni_bool_t
__ni_testbus_container_add_host(ni_dbus_object_t *container, const char *host_path, const char *role)
{
	ni_dbus_variant_t args[2];
	DBusError error = DBUS_ERROR_INIT;
	ni_bool_t result = FALSE;

	ni_assert(container);
	ni_debug_wicked("%s.addHost(%s, %s)", container->path, role, host_path);

	ni_dbus_variant_vector_init(args, 2);
	ni_dbus_variant_set_string(&args[0], role);
	ni_dbus_variant_set_string(&args[1], host_path);
	if (!ni_dbus_object_call_variant(container, NULL, "addHost", 2, args, 0, NULL, &error)) {
		ni_dbus_print_error(&error, "%s.addHost(%s, %s): failed", container->path, role, host_path);
		dbus_error_free(&error);
	} else {
		result = TRUE;
	}

failed:
	ni_dbus_variant_vector_destroy(args, 2);
	return result;
}

ni_dbus_object_t *
ni_testbus_call_container_child_by_name(ni_dbus_object_t *container_object, const ni_dbus_class_t *class, const char *name)
{
	ni_dbus_variant_t args[2];
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_object_t *result = NULL;

	ni_dbus_variant_vector_init(args, 2);

	ni_dbus_variant_set_string(&args[0], class->name);
	ni_dbus_variant_set_string(&args[1], name);
	if (!ni_dbus_object_call_variant(container_object, NULL, "getChildByName", 2, args, 1, &res, &error)) {
		ni_dbus_print_error(&error, "%s.getChildByName(%s, %s): failed",
				container_object->path, class->name, name);
		dbus_error_free(&error);
	} else {
		result = __ni_testbus_handle_path_result(&res, "getChildByName");
	}

	ni_dbus_variant_vector_destroy(args, 2);
	ni_dbus_variant_destroy(&res);
	return result;
}

ni_bool_t
ni_testbus_call_delete(ni_dbus_object_t *object)
{
	DBusError error = DBUS_ERROR_INIT;

	if (!ni_dbus_object_call_variant(object, NULL, "delete", 0, NULL, 0, NULL, &error)) {
		ni_dbus_print_error(&error, "%s.delete(): failed", object->path);
		dbus_error_free(&error);
		return FALSE;
	}
	return TRUE;
}

ni_dbus_object_t *
ni_testbus_call_create_host(const char *name)
{
	ni_dbus_object_t *hostlist_object;

	hostlist_object = ni_testbus_call_get_and_refresh_object(NI_TESTBUS_HOSTLIST_PATH);
	if (!hostlist_object)
		return NULL;

	return __ni_testbus_container_create_child(hostlist_object, "createHost", name);
}

ni_dbus_object_t *
ni_testbus_call_reconnect_host(const char *name, const ni_uuid_t *uuid)
{
	ni_dbus_object_t *hostlist_object;
	ni_dbus_variant_t args[2];
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_object_t *host_object = NULL;

	ni_dbus_variant_vector_init(args, 2);

	hostlist_object = ni_testbus_call_get_and_refresh_object(NI_TESTBUS_HOSTLIST_PATH);
	if (!hostlist_object)
		return NULL;

	ni_dbus_variant_set_string(&args[0], name);
	ni_dbus_variant_set_uuid(&args[1], uuid);
	if (!ni_dbus_object_call_variant(hostlist_object, NULL, "reconnect", 2, args, 1, &res, &error)) {
		ni_dbus_print_error(&error, "%s.reconnect(%s, %s): failed",
				hostlist_object->path, name, ni_uuid_print(uuid));
		dbus_error_free(&error);
		goto failed;
	} else {
		host_object = __ni_testbus_handle_path_result(&res, "reconnect");
		if (!host_object)
			ni_error("reconnect failed");
	}

failed:
	ni_dbus_variant_vector_destroy(args, 2);
	ni_dbus_variant_destroy(&res);
	return host_object;
}

ni_bool_t
ni_testbus_call_remove_host(const char *name)
{
	ni_dbus_object_t *hostlist_object;
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;

	hostlist_object = ni_testbus_call_get_and_refresh_object(NI_TESTBUS_HOSTLIST_PATH);
	if (!hostlist_object)
		return FALSE;

	ni_dbus_variant_set_string(&arg, name);
	if (!ni_dbus_object_call_variant(hostlist_object, NULL, "removeHost", 1, &arg, 0, NULL, &error)) {
		ni_dbus_print_error(&error, "%s.removeHost(%s): failed",
				hostlist_object->path, name);
		dbus_error_free(&error);
	}

	ni_dbus_variant_destroy(&arg);
	return TRUE;
}

ni_dbus_object_t *
ni_testbus_call_create_test(const char *name, ni_dbus_object_t *parent)
{
	if (parent == NULL)
		parent = ni_testbus_call_get_object_and_metadata(NI_TESTBUS_GLOBAL_CONTEXT_PATH);

	return __ni_testbus_container_create_child(parent, "createTest", name);
}

static const ni_dbus_service_t *
__ni_testbus_host_service(void)
{
	static const ni_dbus_service_t *service;

	if (!service) {
		service = ni_objectmodel_service_by_name(NI_TESTBUS_HOST_INTERFACE);
		ni_assert(service);
	}
	return service;
}

static const ni_dbus_variant_t *
__ni_testbus_host_get_cached_property(const ni_dbus_object_t *host_object, const char *name)
{
	const ni_dbus_variant_t *var = NULL;

	var = ni_dbus_object_get_cached_property(host_object, name, __ni_testbus_host_service());
	if (var == NULL) {
		ni_error("host %s has no property named %s", host_object->path, name);
		return NULL;
	}

	return var;
}

static ni_bool_t
__ni_testbus_host_get_cached_string_property(const ni_dbus_object_t *host_object, const char *name, const char **value_p)
{
	const ni_dbus_variant_t *var = NULL;

	if (!(var = __ni_testbus_host_get_cached_property(host_object, name)))
		return FALSE;

	if (!ni_dbus_variant_get_string(var, value_p)) {
		ni_error("host property %s is not of type string", name);
		return FALSE;
	}

	return TRUE;
}

static ni_bool_t
__ni_testbus_host_is_active(const ni_dbus_object_t *host_object)
{
	const char *owner = NULL;

	if (!__ni_testbus_host_get_cached_string_property(host_object, "agent", &owner))
		return FALSE;

	return owner != NULL && *owner != '\0';
}

static ni_bool_t
__ni_testbus_host_is_inuse(const ni_dbus_object_t *host_object, const ni_dbus_object_t *container_object)
{
	const char *host_role;

	if (!__ni_testbus_host_get_cached_string_property(host_object, "role", &host_role))
		return FALSE;

	if (host_role) {
		/* FIXME: we may want to verify whether its owner is the container_object
		 * we want to assign it to.
		 */
		ni_error("host %s already in use (role=%s)", host_object->path, host_role);
		return TRUE;
	}

	return FALSE;
}


static ni_dbus_object_t *
__ni_testbus_host_byname(const char *hostname)
{
	ni_dbus_object_t *host_base_object, *host;
	const ni_dbus_service_t *service;

	host_base_object = ni_testbus_call_get_and_refresh_object(NI_TESTBUS_HOST_BASE_PATH);
	if (!host_base_object)
		return NULL;

	for (host = host_base_object->children; host; host = host->next) {
		const char *this_name;

		if (!__ni_testbus_host_get_cached_string_property(host, "name", &this_name))
			continue;

		if (ni_string_eq(hostname, this_name)) {
			if (!__ni_testbus_host_is_active(host)) {
				ni_error("host %s: no agent running", hostname);
				return NULL;
			}
			return host;
		}
	}

	ni_error("host %s: no host by that name", hostname);
	return NULL;
}

static ni_bool_t
__ni_testbus_host_has_capability(ni_dbus_object_t *host_object, const char *name)
{
	const ni_dbus_variant_t *var;
	unsigned int i;

	if (!(var = __ni_testbus_host_get_cached_property(host_object, "capabilities")))
		return FALSE;

	if (!ni_dbus_variant_is_string_array(var)) {
		ni_error("host property capabilities is not a string array");
		return FALSE;
	}

	if (name == NULL || ni_string_eq(name, "any"))
		return TRUE;

	for (i = 0; i < var->array.len; ++i) {
		const char *this_cap = var->string_array_value[i];

		if (ni_string_eq(name, this_cap))
			return TRUE;
	}

	return FALSE;
}

ni_dbus_object_t *
ni_testbus_call_claim_host_by_name(const char *hostname, ni_dbus_object_t *container_object, const char *role)
{
	ni_dbus_object_t *host_object;

	host_object = __ni_testbus_host_byname(hostname);
	if (!host_object)
		return NULL;

	if (__ni_testbus_host_is_inuse(host_object, container_object))
		return NULL;

	if (!__ni_testbus_container_add_host(container_object, host_object->path, role)) {
		ni_error("failed to claim host %s (%s) in role %s", hostname, host_object->path, role);
		return NULL;
	}

	return host_object;
}

ni_dbus_object_t *
ni_testbus_call_claim_host_by_capability(const char *capability, ni_dbus_object_t *container_object, const char *role)
{
	ni_dbus_object_t *host_base_object, *host_object;
	unsigned int match_count = 0;

	host_base_object = ni_testbus_call_get_and_refresh_object(NI_TESTBUS_HOST_BASE_PATH);
	if (!host_base_object)
		return NULL;

	for (host_object = host_base_object->children; host_object; host_object = host_object->next) {
		if (__ni_testbus_host_has_capability(host_object, capability)) {
			match_count++;

			if (!__ni_testbus_host_is_inuse(host_object, container_object)) {
				if (__ni_testbus_container_add_host(container_object, host_object->path, role))
					return host_object;

				ni_error("failed to claim host %s in role %s", host_object->path, role);
				/* plod on... */
			}
		}
	}

	if (match_count == 0) {
		ni_error("no hosts matching capability \"%s\"", capability? capability : "any");
	} else {
		ni_error("all hosts matching capability \"%s\" are in use (%u total)", capability, match_count);
	}
	return NULL;
}

ni_dbus_object_t *
ni_testbus_agent_create(const char *bus_name)
{
	ni_dbus_client_t *client;
	ni_dbus_object_t *object;

	if (!(client = ni_dbus_client_open(NULL, bus_name))) {
		ni_error("unable to create dbus agent for %s", bus_name);
		return NULL;
	}

	object = ni_dbus_client_object_new(client, NULL,
				NI_TESTBUS_ROOT_PATH, NI_TESTBUS_ROOT_INTERFACE,
				NULL);
	ni_dbus_client_set_root_object(client, object);

	return object;
}

ni_dbus_object_t *
ni_testbus_call_get_agent(const char *hostname)
{
	ni_dbus_object_t *host_base_object, *host;
	const ni_dbus_service_t *service;

	host_base_object = ni_testbus_call_get_and_refresh_object(NI_TESTBUS_HOST_BASE_PATH);
	if (!host_base_object)
		return NULL;

	service = ni_objectmodel_service_by_name(NI_TESTBUS_HOST_INTERFACE);
	ni_assert(service);

	for (host = host_base_object->children; host; host = host->next) {
		const ni_dbus_variant_t *var = NULL;
		const char *value;

		var = ni_dbus_object_get_cached_property(host, "name", service);
		if (var == NULL || !ni_dbus_variant_get_string(var, &value))
			continue;

		if (ni_string_eq(hostname, value)) {
			ni_debug_wicked("host %s object path %s", hostname, host->path);
			var = ni_dbus_object_get_cached_property(host, "agent", service);
			if (var == NULL || !ni_dbus_variant_get_string(var, &value)) {
				ni_error("host has no owner property");
				return FALSE;
			}
			if (!value || !*value) {
				ni_error("host %s: no agent running", hostname);
				return NULL;
			}
			return ni_testbus_agent_create(value);
		}
	}

	ni_error("host %s: no host by that name", hostname);
	return NULL;
}

ni_bool_t
ni_testbus_agent_add_capability(ni_dbus_object_t *host_object, const char *cap)
{
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;
	ni_bool_t rv;

	ni_dbus_variant_set_string(&arg, cap);

	rv = ni_dbus_object_call_variant(host_object, NULL, "addCapability", 1, &arg, 0, NULL, &error);
	if (!rv) {
		ni_dbus_print_error(&error, "%s.addCapability(%s): failed",
				host_object->path, cap);
		dbus_error_free(&error);
	}

	ni_dbus_variant_destroy(&arg);
	return rv;
}

ni_bool_t
ni_testbus_agent_add_capabilities(ni_dbus_object_t *host_object, const ni_string_array_t *array)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		if (!ni_testbus_agent_add_capability(host_object, array->data[i]))
			return FALSE;
	}

	return TRUE;
}

ni_buffer_t *
ni_testbus_agent_retrieve_file(ni_dbus_object_t *agent, const char *path)
{
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	ni_buffer_t *result = NULL;
	ni_dbus_object_t *filesystem;
	uint64_t offset, size;

	filesystem = ni_dbus_object_create(agent, NI_TESTBUS_AGENT_FS_PATH, ni_testbus_filesystem_class(), NULL);
	ni_objectmodel_bind_compatible_interfaces(filesystem);

	ni_dbus_variant_set_string(&arg, path);
	if (!ni_dbus_object_call_variant(filesystem, NULL, "getInfo", 1, &arg, 1, &res, &error)) {
		ni_dbus_print_error(&error, "%s.getFileInfo(%s): failed", agent->path, path);
		dbus_error_free(&error);
		goto out;
	}

	if (!ni_dbus_dict_get_uint64(&res, "size", &size)) {
		ni_error("%s: server didn't return size attribute", path);
		goto out;
	}
	ni_debug_wicked("%s: size=%Lu", path, (unsigned long long) size);

	result = ni_buffer_new(size);
	for (offset = 0; offset < size; ) {
		ni_dbus_variant_t argv[3];
		ni_dbus_variant_vector_init(argv, 3);
		unsigned int count;

		ni_dbus_variant_set_string(&argv[0], path);
		ni_dbus_variant_set_uint64(&argv[1], offset);
		ni_dbus_variant_set_uint32(&argv[2], 4096);

		ni_dbus_variant_destroy(&res);
		if (!ni_dbus_object_call_variant(filesystem, NULL, "retrieve", 3, argv, 1, &res, &error)) {
			ni_dbus_variant_vector_destroy(argv, 3);
			goto out_fail;
		}
		ni_dbus_variant_vector_destroy(argv, 3);

		if (!ni_dbus_variant_is_byte_array(&res)) {
			ni_error("incompatible return type in Filesystem.retrieve()");
			goto out_fail;
		}

		count = ni_buffer_tailroom(result);
		if (count > res.array.len)
			count = res.array.len;
		ni_buffer_put(result, res.byte_array_value, count);
		offset += count;
	}

	ni_debug_wicked("%s: retrieved %u bytes", path, ni_buffer_count(result));

out:
	ni_dbus_variant_destroy(&arg);
	ni_dbus_variant_destroy(&res);
	return result;

out_fail:
	ni_buffer_free(result);
	result = NULL;
	goto out;
}

ni_dbus_object_t *
ni_testbus_call_create_tempfile(const char *name, ni_dbus_object_t *parent)
{
	if (parent == NULL)
		parent = ni_testbus_call_get_object_and_metadata(NI_TESTBUS_GLOBAL_CONTEXT_PATH);

	return __ni_testbus_container_create_child(parent, "createFile", name);
}

ni_bool_t
__ni_testbus_call_upload_file(ni_dbus_object_t *file_object, ni_buffer_t *buffer)
{
	while (ni_buffer_count(buffer)) {
		DBusError error = DBUS_ERROR_INIT;
		ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
		unsigned int count = ni_buffer_count(buffer);

		if (count > 4096)
			count = 4096;
		ni_dbus_variant_set_byte_array(&arg, ni_buffer_head(buffer), count);
		if (!ni_dbus_object_call_variant(file_object, NULL, "append", 1, &arg, 0, NULL, &error)) {
			ni_dbus_print_error(&error, "%s.append() failed", file_object->path);
			dbus_error_free(&error);
			ni_dbus_variant_destroy(&arg);
			return FALSE;
		}

		ni_buffer_pull_head(buffer, count);
		ni_dbus_variant_destroy(&arg);
	}

	return TRUE;
}

ni_bool_t
ni_testbus_call_upload_file(ni_dbus_object_t *file_object, const ni_buffer_t *buffer)
{
	ni_buffer_t copy = *buffer;

	return __ni_testbus_call_upload_file(file_object, &copy);
}

ni_buffer_t *
ni_testbus_call_download_file(ni_dbus_object_t *file_object)
{
	static const unsigned int ioblksize = 4096;
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	ni_buffer_t *result = NULL;
	uint64_t offset = 0;

	ni_debug_wicked("ni_testbus_call_download_file(%s)", file_object->path);
	result = ni_buffer_new(0);
	while (TRUE) {
		ni_dbus_variant_t argv[2];
		ni_dbus_variant_vector_init(argv, 2);
		unsigned int count;

		ni_dbus_variant_set_uint64(&argv[0], offset);
		ni_dbus_variant_set_uint32(&argv[1], ioblksize);

		ni_dbus_variant_destroy(&res);
		if (!ni_dbus_object_call_variant(file_object, NULL, "retrieve", 2, argv, 1, &res, &error)) {
			ni_dbus_print_error(&error, "%s.run(%u @%u): failed", file_object->path, ioblksize, offset);
			ni_dbus_variant_vector_destroy(argv, 3);
			goto out_fail;
		}
		ni_dbus_variant_vector_destroy(argv, 2);

		if (!ni_dbus_variant_is_byte_array(&res)) {
			ni_error("incompatible return type in Filesystem.retrieve()");
			goto out_fail;
		}

		count = res.array.len;
		if (!ni_buffer_ensure_tailroom(result, count))
			goto out_fail;

		ni_buffer_put(result, res.byte_array_value, count);
		offset += count;
		
		if (count < ioblksize)
			break;
	}

	ni_debug_wicked("%s: retrieved %u bytes of data", file_object->path, ni_buffer_count(result));
out:
	ni_dbus_variant_destroy(&res);
	return result;

out_fail:
	ni_buffer_free(result);
	result = NULL;
	goto out;
}

/*
 * Create a command
 */
ni_dbus_object_t *
ni_testbus_call_create_command(ni_dbus_object_t *container_object, const ni_string_array_t *cmd_args)
{
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	ni_dbus_object_t *result = NULL;

	ni_dbus_variant_set_string_array(&arg, (const char **) cmd_args->data, cmd_args->count);
	if (!ni_dbus_object_call_variant(container_object, NULL, "createCommand", 1, &arg, 1, &res, &error)) {
		ni_dbus_print_error(&error, "%s.run(): failed", container_object->path);
		dbus_error_free(&error);
	} else {
		result = __ni_testbus_handle_path_result(&res, "createCommand");
	}

	ni_dbus_variant_destroy(&arg);
	ni_dbus_variant_destroy(&res);
	return result;
}

ni_bool_t
ni_testbus_call_command_set_input(ni_dbus_object_t *cmd_object, const ni_buffer_t *data)
{
	ni_dbus_object_t *file_object;
	ni_buffer_t data_copy;

	file_object = ni_testbus_call_create_tempfile("stdin", cmd_object);
	if (!file_object) {
		ni_error("%s: unable to create stdin", cmd_object->path);
		return FALSE;
	}

	data_copy = *data; /* Need to copy data to allow advancing the head pointer */
	return __ni_testbus_call_upload_file(file_object, &data_copy);
}

/*
 * Handle process completion signals
 */

struct __ni_testbus_process_waitq {
	struct __ni_testbus_process_waitq **prev;
	struct __ni_testbus_process_waitq *next;

	char *			object_path;
	ni_bool_t		done;

	ni_process_exit_info_t *exit_info;
};

static struct __ni_testbus_process_waitq *__ni_testbus_process_waitq;

static void
__ni_testbus_process_waitq_free(struct __ni_testbus_process_waitq *wq)
{
	ni_assert(!wq->prev && !wq->next);
	if (wq->exit_info)
		free(wq->exit_info);
	ni_string_free(&wq->object_path);
	free(wq);
}

static inline void
__ni_testbus_process_waitq_insert(struct __ni_testbus_process_waitq **pos, struct __ni_testbus_process_waitq *wq)
{
	wq->prev = pos;
	wq->next = *pos;
	if (wq->next)
		wq->next->prev = &wq->next;
	*pos = wq;
}

static inline void
__ni_testbus_process_waitq_unlink(struct __ni_testbus_process_waitq *wq)
{
	if (wq->prev) {
		*(wq->prev) = wq->next;
		if (wq->next)
			wq->next->prev = wq->prev;

		wq->next = NULL;
		wq->prev = NULL;
	}
}

static struct __ni_testbus_process_waitq *
__ni_testbus_process_waitq_find(const char *object_path)
{
	struct __ni_testbus_process_waitq *wq;

	for (wq = __ni_testbus_process_waitq; wq; wq = wq->next) {
		if (ni_string_eq(wq->object_path, object_path))
			return wq;
	}

	return wq;
}

static void
__ni_testbus_process_signal(ni_dbus_connection_t *connection, ni_dbus_message_t *msg, void *user_data)
{
	const char *signal_name = dbus_message_get_member(msg);
	const char *object_path = dbus_message_get_path(msg);
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	int argc;

	if (!signal_name)
		return;

	argc = ni_dbus_message_get_args_variants(msg, &arg, 1);
	if (argc < 0) {
		ni_error("%s: cannot extract parameters of signal %s", __func__, signal_name);
		goto out;
	}

	if (ni_string_eq(signal_name, "processExited")) {
		struct __ni_testbus_process_waitq *wq;

		if (argc < 1 || !ni_dbus_variant_is_dict(&arg)) {
			ni_error("%s: bad argument for signal %s()", __func__, signal_name);
			goto out;
		}

		ni_trace("received signal %s from %s", signal_name, object_path);
		if ((wq = __ni_testbus_process_waitq_find(object_path)) != NULL) {
			__ni_testbus_process_waitq_unlink(wq);
			wq->exit_info = ni_testbus_process_exit_info_deserialize(&arg);
			wq->done = TRUE;
		} else {
			ni_trace("spurious signal %s.%s()", object_path, signal_name);
		}
	}

out:
	ni_dbus_variant_destroy(&arg);
}

static struct __ni_testbus_process_waitq *
__ni_testbus_process_wait(const char *object_path)
{
	struct __ni_testbus_process_waitq *wq;

	if (object_path == NULL)
		return NULL;

	if ((wq = __ni_testbus_process_waitq_find(object_path)) == NULL) {
		wq = ni_calloc(1, sizeof(*wq));
		ni_string_dup(&wq->object_path, object_path);

		__ni_testbus_process_waitq_insert(&__ni_testbus_process_waitq, wq);
		ni_assert(__ni_testbus_process_waitq_find(object_path));
	}

	return wq;
}

static void
__ni_testbus_setup_process_handling(void)
{
	static ni_bool_t initialized = FALSE;

	if (initialized)
		return;

	ni_dbus_client_add_signal_handler(ni_call_client,
			NI_TESTBUS_DBUS_BUS_NAME,	/* sender */
			NULL,				/* path */
			NI_TESTBUS_PROCESS_INTERFACE,	/* interface */
			__ni_testbus_process_signal,
			NULL);

	initialized = TRUE;
}

/*
 * Run a command on a remote host
 */
ni_dbus_object_t *
ni_testbus_call_host_run(ni_dbus_object_t *host_object, const ni_dbus_object_t *cmd_object)
{
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	ni_dbus_object_t *result;

	__ni_testbus_setup_process_handling();

	ni_dbus_variant_set_string(&arg, cmd_object->path);

	// FIXME we need to add an argument telling the server to capture stdout/stderr

	if (!ni_dbus_object_call_variant(host_object, NULL, "run", 1, &arg, 1, &res, &error)) {
		ni_dbus_print_error(&error, "%s.run(): failed", host_object->path);
		dbus_error_free(&error);
	} else {
		const char *object_path;

		/* We cannot use __ni_testbus_handle_path_result() here, because
		 * we must not call the DBus event loop before installing the signal
		 * handler for this process.
		 */
		if (!ni_dbus_variant_get_string(&res, &object_path)) {
			ni_error("failed to decode run() response");
			goto failed;
		}

		if (!object_path || !*object_path) {
			ni_error("run() returns empty string");
			goto failed;
		}
		__ni_testbus_process_wait(object_path);

		result = ni_testbus_call_get_and_refresh_object(object_path);
	}

failed:
	ni_dbus_variant_destroy(&arg);
	ni_dbus_variant_destroy(&res);
	return result;
}

ni_bool_t
ni_testbus_wait_for_process(ni_dbus_object_t *proc_object, long timeout_ms, ni_process_exit_info_t *exit_info)
{
	struct __ni_testbus_process_waitq *wq;

	if ((wq = __ni_testbus_process_waitq_find(proc_object->path)) == NULL) {
		ni_error("cannot wait for process %s - not recorded", proc_object->path);
		return FALSE;
	}

	wq->exit_info = exit_info;

	while (TRUE) {
		if (wq->done) {
			ni_debug_wicked("process %s is done", proc_object->path);
			if (exit_info && wq->exit_info)
				*exit_info = *(wq->exit_info);
			__ni_testbus_process_waitq_free(wq);

			if (!ni_dbus_object_refresh_children(proc_object)) {
				ni_error("%s: unable to refresh process object after exit", proc_object->path);
				return FALSE;
			}
			return TRUE;
		}
		if (ni_socket_wait(timeout_ms) < 0)
			ni_fatal("ni_socket_wait failed");
	}

	return FALSE;
}

/*
 * Callback from agent to master: process has exited
 */
ni_bool_t
ni_testbus_call_process_exit(ni_dbus_object_t *proc_object, const ni_process_exit_info_t *exit_info)
{
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	ni_bool_t rv = FALSE;

	ni_dbus_variant_init_dict(&arg);
	if (!ni_testbus_process_exit_info_serialize(exit_info, &arg)) {
		ni_error("failed to serialize exit info");
		goto failed;
	}

	rv = ni_dbus_object_call_variant(proc_object, NULL, "setExitInfo", 1, &arg, 0, NULL, &error);
	if (!rv) {
		ni_dbus_print_error(&error, "%s.setExitInfo(): failed", proc_object->path);
		dbus_error_free(&error);
	}

failed:
	ni_dbus_variant_destroy(&arg);
	return rv;
}
