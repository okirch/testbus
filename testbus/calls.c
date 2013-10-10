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
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/dbus-model.h>

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
static ni_dbus_object_t *	ni_call_root_object;

/*
 * Create the client
 */
void
ni_call_init_client(ni_dbus_client_t *client)
{
	ni_assert(ni_call_root_object == NULL);

	if (client == NULL) {
		client = ni_objectmodel_create_client();
		if (!client)
			ni_fatal("Unable to connect to dbus service");
	}
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
		const char *value;

		if (!ni_dbus_variant_get_string(&res, &value)) {
			ni_error("failed to decode %s() response", method_name);
			goto failed;
		}

		if (!value || !*value) {
			ni_error("%s() returns empty string", method_name);
			goto failed;
		}
		result = ni_testbus_call_get_and_refresh_object(value);
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
		const char *value;

		if (!ni_dbus_variant_get_string(&res, &value)) {
			ni_error("failed to decode createHost() response");
			goto failed;
		}

		if (!value || !*value) {
			ni_error("reconnect failed");
			goto failed;
		}
		host_object = ni_testbus_call_get_and_refresh_object(value);
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
	uint64_t offset;

	result = ni_buffer_new(0);
	while (TRUE) {
		ni_dbus_variant_t argv[2];
		ni_dbus_variant_vector_init(argv, 2);
		unsigned int count;

		ni_dbus_variant_set_uint64(&argv[0], offset);
		ni_dbus_variant_set_uint32(&argv[1], ioblksize);

		ni_dbus_variant_destroy(&res);
		if (!ni_dbus_object_call_variant(file_object, NULL, "read", 2, argv, 1, &res, &error)) {
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
		const char *value;

		if (!ni_dbus_variant_get_string(&res, &value)) {
			ni_error("failed to decode createCommand() response");
		} else
		if (!value || !*value) {
			ni_error("createCommand() returns empty string");
		} else {
			result = ni_testbus_call_get_and_refresh_object(value);
		}
	}

	ni_dbus_variant_destroy(&arg);
	ni_dbus_variant_destroy(&res);
	return result;
}

/*
 * Run a command on a remote host
 */
ni_bool_t
ni_testbus_call_host_run(ni_dbus_object_t *host_object, const ni_dbus_object_t *cmd_object)
{
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	ni_bool_t rv;

	ni_dbus_variant_set_string(&arg, cmd_object->path);
	if (!ni_dbus_object_call_variant(host_object, NULL, "run", 1, &arg, 0, NULL, &error)) {
		ni_dbus_print_error(&error, "%s.run(): failed", host_object->path);
		dbus_error_free(&error);
		return FALSE;
	}

	ni_dbus_variant_destroy(&arg);
	return TRUE;
}
