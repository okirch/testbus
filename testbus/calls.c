/*
 * No REST for the wicked!
 *
 * Client-side functions for calling the wicked server.
 *
 * Copyright (C) 2010-2014 Olaf Kirch <okir@suse.de>
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
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/dbus-model.h>
#include <dborb/socket.h>
#include <dborb/process.h>
#include <testbus/model.h>
#include <testbus/client.h>
#include <testbus/process.h>
#include <testbus/monitor.h>

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
static ni_dbus_client_t *	ni_testbus_client_handle;
static ni_dbus_object_t *	ni_testbus_client_root_object;

/*
 * Create the client
 */
void
ni_testbus_client_init(ni_dbus_client_t *client)
{
	ni_assert(ni_testbus_client_root_object == NULL);
	ni_assert(ni_testbus_client_handle == NULL);

	if (client == NULL) {
		client = ni_objectmodel_create_client();
		if (!client)
			ni_fatal("Unable to connect to dbus service");
	}
	ni_testbus_client_handle = client;
	ni_testbus_client_root_object = ni_dbus_client_get_root_object(client);
}


/*
 * Create the client and return the handle of the root object
 */
static ni_dbus_object_t *
ni_testbus_client_get_root(void)
{
	ni_assert(ni_testbus_client_root_object);
	return ni_testbus_client_root_object;
}

/*
 * Obtain an object handle, generic version
 */
static ni_dbus_object_t *
__ni_testbus_client_get_proxy_object(const ni_dbus_service_t *service, const char *relative_path)
{
	ni_dbus_object_t *root_object, *child;

	if (!(root_object = ni_testbus_client_get_root()))
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
__ni_testbus_call_proxy_object_by_path(const char *path, const char *service_name)
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

	object = __ni_testbus_client_get_proxy_object(service, relative_path);

	ni_dbus_object_set_default_interface(object, service_name);
	return object;
}

enum {
	NI_TESTBUS_GET_NOUPDATE		= 0,
	NI_TESTBUS_GET_INTERFACE	= 1,
	NI_TESTBUS_GET_DATA		= 2,
};

static ni_dbus_object_t *
__ni_testbus_client_get_object(ni_dbus_object_t *root_object, const char *path, unsigned int how)
{
	ni_dbus_object_t *result;

	if (!root_object && !(root_object = ni_testbus_client_get_root()))
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
ni_testbus_client_get_object(const char *path)
{
	return __ni_testbus_client_get_object(NULL, path, NI_TESTBUS_GET_NOUPDATE);
}

ni_dbus_object_t *
ni_testbus_client_get_and_refresh_object(const char *path)
{
	return __ni_testbus_client_get_object(NULL, path, NI_TESTBUS_GET_DATA);
}

ni_dbus_object_t *
ni_testbus_client_get_object_and_metadata(const char *path)
{
	return __ni_testbus_client_get_object(NULL, path, NI_TESTBUS_GET_INTERFACE);
}

ni_dbus_object_t *
ni_testbus_client_get_container(const char *path)
{
	ni_dbus_object_t *object;

	object = ni_testbus_client_get_object_and_metadata(path);
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
	return ni_testbus_client_get_and_refresh_object(value);
}

static ni_dbus_object_t *
__ni_testbus_client_container_create_child(ni_dbus_object_t *container, const char *method_name,
			const char *name, const ni_dbus_variant_t *extraArg)
{
	ni_dbus_variant_t args[2];
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_object_t *result = NULL;
	int argc = 1;

	ni_assert(container);

	memset(args, 0, sizeof(*args));
	ni_dbus_variant_set_string(&args[0], name);
	if (extraArg)
		args[argc++] = *extraArg;

	if (!ni_dbus_object_call_variant(container, NULL, method_name, argc, args, 1, &res, &error)) {
		ni_dbus_print_error(&error, "%s.%s(%s): failed", container->path, method_name, name);
		dbus_error_free(&error);
	} else {
		result = __ni_testbus_handle_path_result(&res, method_name);
	}

	ni_dbus_variant_destroy(&args[0]);
	ni_dbus_variant_destroy(&res);
	return result;
}

static ni_bool_t
__ni_testbus_client_container_add_host(ni_dbus_object_t *container, const char *host_path, const char *role)
{
	ni_dbus_variant_t args[2];
	DBusError error = DBUS_ERROR_INIT;
	ni_bool_t result = FALSE;

	ni_assert(container);
	ni_debug_testbus("%s.addHost(%s, %s)", container->path, role, host_path);

	ni_dbus_variant_vector_init(args, 2);
	ni_dbus_variant_set_string(&args[0], role);
	ni_dbus_variant_set_string(&args[1], host_path);
	if (!ni_dbus_object_call_variant(container, NULL, "addHost", 2, args, 0, NULL, &error)) {
		ni_dbus_print_error(&error, "%s.addHost(%s, %s): failed", container->path, role, host_path);
		dbus_error_free(&error);
	} else {
		result = TRUE;
	}

	ni_dbus_variant_vector_destroy(args, 2);
	return result;
}

ni_dbus_object_t *
ni_testbus_client_container_child_by_name(ni_dbus_object_t *container_object, const ni_dbus_class_t *class, const char *name)
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
ni_testbus_client_setenv(ni_dbus_object_t *container, const char *name, const char *value)
{
	ni_dbus_variant_t args[2];
	DBusError error = DBUS_ERROR_INIT;
	ni_bool_t result = FALSE;

	ni_assert(container);

	ni_dbus_variant_vector_init(args, 2);
	ni_dbus_variant_set_string(&args[0], name);
	ni_dbus_variant_set_string(&args[1], value);
	if (!ni_dbus_object_call_variant(container, NULL, "setenv", 2, args, 0, NULL, &error)) {
		ni_dbus_print_error(&error, "%s.setenv(%s, %s): failed", container->path, name, value);
		dbus_error_free(&error);
	} else {
		result = TRUE;
	}

	ni_dbus_variant_vector_destroy(args, 2);
	return result;
}

char *
ni_testbus_client_getenv(ni_dbus_object_t *container, const char *name)
{
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;
	char *result = NULL;

	ni_assert(container);

	ni_dbus_variant_set_string(&arg, name);
	if (!ni_dbus_object_call_variant(container, NULL, "getenv", 1, &arg, 1, &res, &error)) {
		ni_dbus_print_error(&error, "%s.getenv(%s): failed", container->path, name);
		dbus_error_free(&error);
	} else {
		const char *resp;

		if (ni_dbus_variant_get_string(&res, &resp)) {
			result = strdup(resp? resp : "");
		} else {
			ni_error("%s.getenv(%s): not a string value", container->path, name);
		}
	}

	ni_dbus_variant_destroy(&arg);
	ni_dbus_variant_destroy(&res);
	return result;
}

ni_bool_t
ni_testbus_client_eventlog_append(ni_dbus_object_t *object, const ni_event_t *ev)
{
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;
	ni_bool_t rv;

	if (!ni_testbus_event_serialize(ev, &arg)) {
		ni_error("%s: failed to serialize event", __func__);
		return FALSE;
	}

	rv = ni_dbus_object_call_variant(object, NI_TESTBUS_EVENTLOG_INTERFACE, "add", 1, &arg, 0, NULL, &error);
	if (!rv) {
		ni_dbus_print_error(&error, "%s: failed to add event", object->path);
		dbus_error_free(&error);
	}

	ni_dbus_variant_destroy(&arg);
	return rv;
}

ni_bool_t
ni_testbus_client_eventlog_purge(ni_dbus_object_t *object, unsigned int until_seq)
{
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;
	ni_bool_t rv;

	ni_dbus_variant_set_uint32(&arg, until_seq);
	rv = ni_dbus_object_call_variant(object, NI_TESTBUS_EVENTLOG_INTERFACE, "purge", 1, &arg, 0, NULL, &error);
	if (!rv) {
		ni_dbus_print_error(&error, "%s: failed to purge event log", object->path);
		dbus_error_free(&error);
	}

	ni_dbus_variant_destroy(&arg);
	return rv;
}

ni_bool_t
ni_testbus_client_delete(ni_dbus_object_t *object)
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
ni_testbus_client_create_host(const char *name)
{
	ni_dbus_object_t *hostlist_object;

	hostlist_object = ni_testbus_client_get_and_refresh_object(NI_TESTBUS_HOSTLIST_PATH);
	if (!hostlist_object)
		return NULL;

	return __ni_testbus_client_container_create_child(hostlist_object, "createHost", name, NULL);
}

ni_dbus_object_t *
ni_testbus_client_reconnect_host(const char *name)
{
	ni_dbus_object_t *hostlist_object;
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_object_t *host_object = NULL;

	hostlist_object = ni_testbus_client_get_and_refresh_object(NI_TESTBUS_HOSTLIST_PATH);
	if (!hostlist_object)
		return NULL;

	ni_dbus_variant_set_string(&arg, name);
	if (!ni_dbus_object_call_variant(hostlist_object, NULL, "reconnect", 1, &arg, 1, &res, &error)) {
		ni_dbus_print_error(&error, "%s.reconnect(%s): failed", hostlist_object->path, name);
		dbus_error_free(&error);
		goto failed;
	} else {
		host_object = __ni_testbus_handle_path_result(&res, "reconnect");
		if (!host_object)
			ni_error("reconnect failed");
	}

failed:
	ni_dbus_variant_destroy(&arg);
	ni_dbus_variant_destroy(&res);
	return host_object;
}

ni_bool_t
ni_testbus_client_remove_host(const char *name)
{
	ni_dbus_object_t *hostlist_object;
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;

	hostlist_object = ni_testbus_client_get_and_refresh_object(NI_TESTBUS_HOSTLIST_PATH);
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
ni_testbus_client_create_test(const char *name, ni_dbus_object_t *parent)
{
	if (parent == NULL)
		parent = ni_testbus_client_get_object_and_metadata(NI_TESTBUS_GLOBAL_CONTEXT_PATH);

	return __ni_testbus_client_container_create_child(parent, "createTest", name, NULL);
}

static inline ni_bool_t
__match_string_property(const ni_dbus_object_t *object, const ni_dbus_service_t *service, const char *name, const char *match_value)
{
	const ni_dbus_variant_t *var;
	const char *property_value;

	var = ni_dbus_object_get_cached_property(object, name, service);
	if (var == NULL)
		return FALSE;
	if (!ni_dbus_variant_get_string(var, &property_value))
		return FALSE;
	return ni_string_eq(match_value, property_value);
}

static const ni_dbus_variant_t *
__ni_testbus_host_get_cached_property(const ni_dbus_object_t *host_object, const char *name)
{
	const ni_dbus_variant_t *var = NULL;

	var = ni_dbus_object_get_cached_property(host_object, name, ni_testbus_host_interface());
	if (var == NULL) {
		ni_debug_testbus("host %s has no property named %s", host_object->path, name);
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
__ni_testbus_host_get_cached_boolean_property(const ni_dbus_object_t *host_object, const char *name, dbus_bool_t *value_p)
{
	const ni_dbus_variant_t *var = NULL;

	if (!(var = __ni_testbus_host_get_cached_property(host_object, name)))
		return FALSE;

	if (!ni_dbus_variant_get_bool(var, value_p)) {
		ni_error("host property %s is not of type boolean", name);
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
__ni_testbus_host_is_ready(const ni_dbus_object_t *host_object)
{
	dbus_bool_t host_ready;

	if (!__ni_testbus_host_get_cached_boolean_property(host_object, "ready", &host_ready))
		return FALSE;

	return host_ready;
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
		ni_debug_testbus("host %s already in use (role=%s)", host_object->path, host_role);
		return TRUE;
	}

	return FALSE;
}


static ni_dbus_object_t *
__ni_testbus_host_byname(const char *hostname)
{
	ni_dbus_object_t *host_base_object, *host;

	host_base_object = ni_testbus_client_get_and_refresh_object(NI_TESTBUS_HOST_BASE_PATH);
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
ni_testbus_client_claim_host_by_name(const char *hostname, ni_dbus_object_t *container_object, const char *role)
{
	ni_dbus_object_t *host_object;

	host_object = __ni_testbus_host_byname(hostname);
	if (!host_object)
		return NULL;

	if (__ni_testbus_host_is_inuse(host_object, container_object))
		return NULL;

	if (!__ni_testbus_client_container_add_host(container_object, host_object->path, role)) {
		ni_error("failed to claim host %s (%s) in role %s", hostname, host_object->path, role);
		return NULL;
	}

	return host_object;
}

/*
 * Signal handling needed to wait for agent(s) to become ready
 */
static void
__ni_testbus_host_signal(ni_dbus_connection_t *connection, ni_dbus_message_t *msg, void *user_data)
{
	const char *signal_name = dbus_message_get_member(msg);
	const char *object_path = dbus_message_get_path(msg);

	if (!signal_name)
		return;

	ni_debug_testbus("received %s.%s() signal", object_path, signal_name);
	/* Nothing else to be done - the ni_socket_wait should return now and allow
	 * us to loop back and examine the host list once more */
}

static void
__ni_testbus_setup_host_signal_handler(void)
{
	static ni_bool_t initialized = FALSE;

	if (initialized)
		return;

	ni_dbus_client_add_signal_handler(ni_testbus_client_handle,
			NI_TESTBUS_DBUS_BUS_NAME,	/* sender */
			NULL,				/* path */
			NI_TESTBUS_HOST_INTERFACE,	/* interface */
			__ni_testbus_host_signal,
			NULL);

	initialized = TRUE;
}


/*
 * Simple timeout handling
 */
static void
__ni_testbus_wait_timeout(void *user_data, const ni_timer_t *timer)
{
	ni_testbus_client_timeout_t *timeout = user_data;

	timeout->handle = NULL;

	if (timeout->timedout)
		timeout->timedout(timeout->user_data);
}

static void
__ni_testbus_wait_setup(ni_testbus_client_timeout_t *timeout)
{
	__ni_testbus_setup_host_signal_handler();

	ni_debug_testbus("register timer for %u ms", timeout->timeout_msec);
	timeout->handle = ni_timer_register(timeout->timeout_msec, __ni_testbus_wait_timeout, timeout);
}

static void
__ni_testbus_wait_cancel(ni_testbus_client_timeout_t *timeout)
{
	if (timeout && timeout->handle) {
		ni_timer_cancel((const ni_timer_t *) timeout->handle);
		timeout->handle = NULL;
	}
}

void
ni_testbus_client_timeout_init(ni_testbus_client_timeout_t *timeout, unsigned int msec)
{
	memset(timeout, 0, sizeof(*timeout));
	timeout->timeout_msec = msec;
}

void
ni_testbus_client_timeout_destroy(ni_testbus_client_timeout_t *timeout, unsigned int msec)
{
	__ni_testbus_wait_cancel(timeout);
}

ni_dbus_object_t *
ni_testbus_client_claim_host_by_capability(const char *capability, ni_dbus_object_t *container_object,
						const char *role, ni_testbus_client_timeout_t *timeout)
{
	ni_dbus_object_t *host_base_object, *host_object;
	unsigned int match_count = 0;

	if (timeout)
		__ni_testbus_wait_setup(timeout);

	while (TRUE) {
		match_count = 0;

		host_base_object = ni_testbus_client_get_and_refresh_object(NI_TESTBUS_HOST_BASE_PATH);
		if (!host_base_object)
			return NULL;

		for (host_object = host_base_object->children; host_object; host_object = host_object->next) {
			if (!__ni_testbus_host_is_ready(host_object))
				continue;
			if (__ni_testbus_host_has_capability(host_object, capability)) {
				match_count++;

				if (!__ni_testbus_host_is_inuse(host_object, container_object)) {
					if (__ni_testbus_client_container_add_host(container_object, host_object->path, role)) {
						__ni_testbus_wait_cancel(timeout);
						return host_object;
					}

					ni_error("failed to claim host %s in role %s", host_object->path, role);
					/* plod on... */
				}
			}
		}

		if (!timeout || !timeout->handle) {
			break;
		} else {
			long waitfor = -1;

			ni_debug_dbus("waiting for host(s) to come online");
			if (timeout->busy_wait)
				waitfor = timeout->busy_wait(timeout->user_data);
			ni_socket_wait(waitfor);
		}
	}

	if (match_count == 0) {
		ni_error("no hosts matching capability \"%s\"", capability? capability : "any");
	} else {
		ni_error("all hosts matching capability \"%s\" are in use (%u total)", capability, match_count);
	}
	__ni_testbus_wait_cancel(timeout);
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

/*
 * Given a hostname or object handle, return an agent object
 */
static ni_dbus_object_t *
__ni_testbus_client_agent_for_host(const ni_dbus_object_t *host, const char *hostname)
{
	const ni_dbus_variant_t *var;
	const char *value;

	var = ni_dbus_object_get_cached_property(host, "agent", ni_testbus_host_interface());
	if (var == NULL || !ni_dbus_variant_get_string(var, &value)) {
		ni_error("host has no owner property");
		return FALSE;
	}
	if (!value || !*value) {
		ni_error("host %s: no agent running", hostname);
		return NULL;
	}
	ni_debug_testbus("agent bus name for %s is %s", hostname, value);
	return ni_testbus_agent_create(value);
}

ni_dbus_object_t *
ni_testbus_client_get_agent(const char *hostname)
{
	ni_dbus_object_t *host_base_object, *host;
	const ni_dbus_service_t *service;

	if (!strncasecmp(hostname, NI_TESTBUS_HOST_BASE_PATH, sizeof(NI_TESTBUS_HOST_BASE_PATH) - 1)) {
		host = ni_testbus_client_get_and_refresh_object(hostname);
		return __ni_testbus_client_agent_for_host(host, hostname);
	}

	host_base_object = ni_testbus_client_get_and_refresh_object(NI_TESTBUS_HOST_BASE_PATH);
	if (!host_base_object)
		return NULL;

	service = ni_testbus_host_interface();
	ni_assert(service);

	for (host = host_base_object->children; host; host = host->next) {
		if (__match_string_property(host, service, "name", hostname)
		 || __match_string_property(host, service, "role", hostname))
			return __ni_testbus_client_agent_for_host(host, hostname);
	}

	ni_error("host %s: no host by that name", hostname);
	return NULL;
}

/*
 * This is function is called by the agent to register a capability
 * with the master.
 */
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

/*
 * This is function is called by the agent to register a list of capabilities
 * with the master.
 */
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

/*
 * This is function is called by the agent to add variables to the host's environment
 * in the master.
 */
ni_bool_t
ni_testbus_agent_add_environment(ni_dbus_object_t *host_object, const ni_var_array_t *array)
{
	unsigned int i;
	ni_var_t *var;

	for (i = 0, var = array->data; i < array->count; ++i, ++var) {
		if (!ni_testbus_client_setenv(host_object, var->name, var->value))
			return FALSE;
	}

	return TRUE;
}

/*
 * Client function: download a file directly from an agent's file system
 */
ni_buffer_t *
ni_testbus_client_agent_download_file(ni_dbus_object_t *agent, const char *path)
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
	ni_debug_testbus("%s: size=%Lu", path, (unsigned long long) size);

	result = ni_buffer_new(size);
	for (offset = 0; offset < size; ) {
		ni_dbus_variant_t argv[3];
		ni_dbus_variant_vector_init(argv, 3);
		unsigned int count;

		ni_dbus_variant_set_string(&argv[0], path);
		ni_dbus_variant_set_uint64(&argv[1], offset);
		ni_dbus_variant_set_uint32(&argv[2], 4096);

		ni_dbus_variant_destroy(&res);
		if (!ni_dbus_object_call_variant(filesystem, NULL, "download", 3, argv, 1, &res, &error)) {
			ni_dbus_variant_vector_destroy(argv, 3);
			goto out_fail;
		}
		ni_dbus_variant_vector_destroy(argv, 3);

		if (!ni_dbus_variant_is_byte_array(&res)) {
			ni_error("incompatible return type in Filesystem.download()");
			goto out_fail;
		}

		count = ni_buffer_tailroom(result);
		if (count > res.array.len)
			count = res.array.len;
		ni_buffer_put(result, res.byte_array_value, count);
		offset += count;
	}

	ni_debug_testbus("%s: downloaded %u bytes", path, ni_buffer_count(result));

out:
	ni_dbus_variant_destroy(&arg);
	ni_dbus_variant_destroy(&res);
	return result;

out_fail:
	ni_buffer_free(result);
	result = NULL;
	goto out;
}

/*
 * Client function: upload a file directly to an agent's file system
 */
ni_bool_t
ni_testbus_client_agent_upload_file(ni_dbus_object_t *agent, const char *path, const ni_buffer_t *wbuf)
{
	DBusError error = DBUS_ERROR_INIT;
	ni_buffer_t data = *wbuf;
	ni_dbus_object_t *filesystem;
	uint64_t offset = 0;

	filesystem = ni_dbus_object_create(agent, NI_TESTBUS_AGENT_FS_PATH, ni_testbus_filesystem_class(), NULL);
	ni_objectmodel_bind_compatible_interfaces(filesystem);

	while (ni_buffer_count(&data)) {
		static const unsigned int wblksz = 4096;
		ni_dbus_variant_t argv[3];
		unsigned int count;

		count = ni_buffer_count(&data);
		if (count > wblksz)
			count = wblksz;

		ni_dbus_variant_vector_init(argv, 3);
		ni_dbus_variant_set_string(&argv[0], path);
		ni_dbus_variant_set_uint64(&argv[1], offset);
		ni_dbus_variant_set_byte_array(&argv[2], ni_buffer_head(&data), count);

		if (!ni_dbus_object_call_variant(filesystem, NULL, "upload", 3, argv, 0, NULL, &error)) {
			ni_dbus_print_error(&error, "%s.upload(%s, %Lu, %u) failed", filesystem->path, path,
							(unsigned long long) offset, count);
			dbus_error_free(&error);
			ni_dbus_variant_vector_destroy(argv, 3);
			return FALSE;
		}
		ni_dbus_variant_vector_destroy(argv, 3);

		ni_buffer_pull_head(&data, count);
	}

	ni_debug_testbus("%s: uploaded %u bytes", path, ni_buffer_count(wbuf));
	return TRUE;
}

ni_dbus_object_t *
ni_testbus_client_create_tempfile(const char *name, unsigned int mode, ni_dbus_object_t *parent)
{
	ni_dbus_variant_t marg = NI_DBUS_VARIANT_INIT;
	ni_dbus_object_t *result;

	if (parent == NULL)
		parent = ni_testbus_client_get_object_and_metadata(NI_TESTBUS_GLOBAL_CONTEXT_PATH);

	ni_dbus_variant_set_uint32(&marg, mode);
	result = __ni_testbus_client_container_create_child(parent, "createFile", name, &marg);
	ni_dbus_variant_destroy(&marg);
	return result;
}

ni_bool_t
__ni_testbus_client_upload_file(ni_dbus_object_t *file_object, ni_buffer_t *buffer)
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
ni_testbus_client_upload_file(ni_dbus_object_t *file_object, const ni_buffer_t *buffer)
{
	ni_buffer_t copy = *buffer;

	return __ni_testbus_client_upload_file(file_object, &copy);
}

ni_buffer_t *
ni_testbus_client_download_file(ni_dbus_object_t *file_object)
{
	static const unsigned int ioblksize = 4096;
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	ni_buffer_t *result = NULL;
	uint64_t offset = 0;

	ni_debug_testbus("ni_testbus_client_download_file(%s)", file_object->path);
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

	ni_debug_testbus("%s: retrieved %u bytes of data", file_object->path, ni_buffer_count(result));
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
ni_testbus_client_create_command(ni_dbus_object_t *container_object, const ni_string_array_t *cmd_args, ni_bool_t use_terminal)
{
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_variant_t argv[2];
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	ni_dbus_object_t *result = NULL;

	ni_dbus_variant_vector_init(argv, 2);

	ni_dbus_variant_set_string_array(&argv[0], (const char **) cmd_args->data, cmd_args->count);
	ni_dbus_variant_init_dict(&argv[1]);

	if (use_terminal)
		ni_dbus_dict_add_bool(&argv[1], "use-terminal", TRUE);

	if (!ni_dbus_object_call_variant(container_object, NULL, "createCommand", 2, argv, 1, &res, &error)) {
		ni_dbus_print_error(&error, "%s.run(): failed", container_object->path);
		dbus_error_free(&error);
	} else {
		result = __ni_testbus_handle_path_result(&res, "createCommand");
	}

	ni_dbus_variant_vector_destroy(argv, 2);
	ni_dbus_variant_destroy(&res);
	return result;
}

ni_bool_t
ni_testbus_client_command_add_file(ni_dbus_object_t *cmd_object, const char *name, const ni_buffer_t *data, unsigned int mode)
{
	ni_dbus_object_t *file_object;

	file_object = ni_testbus_client_create_tempfile(name, mode, cmd_object);
	if (!file_object) {
		ni_error("%s: unable to create input file \"%s\"", cmd_object->path, name);
		return FALSE;
	}

	if (mode & NI_TESTBUS_FILE_READ) {
		ni_buffer_t data_copy;

		data_copy = *data; /* Need to copy data to allow advancing the head pointer */
		if (!__ni_testbus_client_upload_file(file_object, &data_copy))
			return FALSE;
	}

	return TRUE;
}

/*
 * Handle process completion signals
 */

typedef struct ni_testbus_waitq ni_testbus_waitq_t;
typedef struct ni_testbus_wait_queue ni_testbus_wait_queue_t;

struct ni_testbus_waitq {
	ni_testbus_waitq_t **	prev;
	ni_testbus_waitq_t *	next;

	char *			object_path;
	ni_bool_t		done;

	ni_process_exit_info_t *exit_info;
};

struct ni_testbus_wait_queue {
	ni_testbus_waitq_t *	head;
};

static ni_testbus_wait_queue_t	ni_testbus_waitq;
static ni_testbus_wait_queue_t *ni_testbus_spurious_waitq;

static ni_testbus_waitq_t *
ni_testbus_waitq_new(const char *object_path)
{
	ni_testbus_waitq_t *wq;

	wq = ni_malloc(sizeof(*wq));
	ni_string_dup(&wq->object_path, object_path);

	return wq;
}

static void
ni_testbus_waitq_free(ni_testbus_waitq_t *wq)
{
	ni_assert(!wq->prev && !wq->next);
	if (wq->exit_info)
		free(wq->exit_info);
	wq->exit_info = NULL;
	ni_string_free(&wq->object_path);
	free(wq);
}

static inline void
__ni_testbus_waitq_insert(ni_testbus_waitq_t **pos, ni_testbus_waitq_t *wq)
{
	wq->prev = pos;
	wq->next = *pos;
	if (wq->next)
		wq->next->prev = &wq->next;
	*pos = wq;
}

static inline void
ni_testbus_waitq_insert(ni_testbus_wait_queue_t *q, ni_testbus_waitq_t *wq)
{
	__ni_testbus_waitq_insert(&q->head, wq);
}

static inline void
ni_testbus_waitq_unlink(ni_testbus_waitq_t *wq)
{
	if (wq->prev) {
		*(wq->prev) = wq->next;
		if (wq->next)
			wq->next->prev = wq->prev;

		wq->next = NULL;
		wq->prev = NULL;
	}
}

static ni_testbus_waitq_t *
__ni_testbus_waitq_find(ni_testbus_wait_queue_t *q, const char *object_path)
{
	ni_testbus_waitq_t *wq;

	for (wq = q->head; wq; wq = wq->next) {
		if (ni_string_eq(wq->object_path, object_path))
			return wq;
	}

	return wq;
}

static ni_testbus_waitq_t *
ni_testbus_waitq_find(const char *object_path)
{
	return __ni_testbus_waitq_find(&ni_testbus_waitq, object_path);
}

static void
ni_testbus_wait_queue_destroy(ni_testbus_wait_queue_t *q)
{
	ni_testbus_waitq_t *wq;

	while ((wq = q->head) != NULL) {
		ni_testbus_waitq_unlink(wq);
		ni_testbus_waitq_free(wq);
	}
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
		ni_testbus_waitq_t *wq = NULL;

		if (argc < 1 || !ni_dbus_variant_is_dict(&arg)) {
			ni_error("%s: bad argument for signal %s()", __func__, signal_name);
			goto out;
		}

		ni_debug_testbus("received signal %s from %s", signal_name, object_path);
		if ((wq = ni_testbus_waitq_find(object_path)) == NULL) {
			if (ni_testbus_spurious_waitq) {
				wq = ni_testbus_waitq_new(object_path);
				ni_testbus_waitq_insert(ni_testbus_spurious_waitq, wq);
			} else {
				ni_warn("spurious signal %s.%s()", object_path, signal_name);
			}
		} else if (wq->done) {
			ni_warn("duplicate signal %s.%s()", object_path, signal_name);
		}

		if (wq) {
			wq->exit_info = ni_testbus_process_exit_info_deserialize(&arg);
			wq->done = TRUE;
		}
	}

out:
	ni_dbus_variant_destroy(&arg);
}

static ni_testbus_waitq_t *
__ni_testbus_process_wait(const char *object_path)
{
	ni_testbus_waitq_t *wq = NULL;

	if (object_path == NULL)
		return NULL;

	/* We may receive the exit notification before we receive the response to
	 * our run() command. Not sure how this happens, but the dbus library seems
	 * to be doing some funny reordering.
	 * So what we do is:
	 *  -	during the DBus run() call, we collect all signals for unknown processes
	 *	in a "spurious_waitq".
	 *  -	When the run() call returns, we call __ni_testbus_process_wait() to establish
	 *	the regular waitq entry. So we end up in this place
	 *  -	therefore, we need to check here if the process we want to wait for
	 *	has already signaled its exit status
	 */
	if (ni_testbus_spurious_waitq) {
		wq = __ni_testbus_waitq_find(ni_testbus_spurious_waitq, object_path);
		if (wq != NULL) {
			ni_testbus_waitq_unlink(wq);
			ni_testbus_waitq_insert(&ni_testbus_waitq, wq);
		}
	}
	if (wq == NULL)
		wq = ni_testbus_waitq_find(object_path);

	if (wq == NULL) {
		wq = ni_testbus_waitq_new(object_path);

		ni_testbus_waitq_insert(&ni_testbus_waitq, wq);
	}

	return wq;
}

static void
__ni_testbus_setup_process_handling(void)
{
	static ni_bool_t initialized = FALSE;

	if (initialized)
		return;

	ni_dbus_client_add_signal_handler(ni_testbus_client_handle,
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
ni_testbus_client_host_run(ni_dbus_object_t *host_object, const ni_dbus_object_t *cmd_object)
{
	DBusError error = DBUS_ERROR_INIT;
	ni_testbus_wait_queue_t spurious_queue = { .head = NULL };
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	ni_dbus_object_t *result = NULL;

	__ni_testbus_setup_process_handling();
	ni_testbus_spurious_waitq = &spurious_queue;

	ni_dbus_variant_set_string(&arg, cmd_object->path);

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

		result = ni_testbus_client_get_and_refresh_object(object_path);
	}

failed:
	ni_testbus_wait_queue_destroy(&spurious_queue);
	ni_testbus_spurious_waitq = NULL;
	ni_dbus_variant_destroy(&arg);
	ni_dbus_variant_destroy(&res);
	return result;
}

ni_bool_t
ni_testbus_wait_for_process(ni_dbus_object_t *proc_object, long timeout_ms, ni_process_exit_info_t *exit_info)
{
	ni_testbus_waitq_t *wq;

	if ((wq = ni_testbus_waitq_find(proc_object->path)) == NULL) {
		ni_error("cannot wait for process %s - not recorded", proc_object->path);
		return FALSE;
	}

	while (TRUE) {
		if (wq->done) {
			ni_debug_testbus("process %s is done", proc_object->path);
			if (exit_info && wq->exit_info)
				*exit_info = *(wq->exit_info);
			ni_testbus_waitq_unlink(wq);
			ni_testbus_waitq_free(wq);

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
ni_testbus_client_process_exit(ni_dbus_object_t *proc_object, const ni_process_exit_info_t *exit_info)
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

/*
 * shut down a remote host
 */
ni_bool_t
ni_testbus_client_host_shutdown(ni_dbus_object_t *host_object, ni_bool_t reboot)
{
	const char *method_name = reboot? "reboot" : "shutdown";
	DBusError error = DBUS_ERROR_INIT;
	ni_bool_t result;

	result = ni_dbus_object_call_variant(host_object, NULL, method_name, 0, NULL, 0, NULL, &error);
	if (!result) {
		ni_dbus_print_error(&error, "%s.%s(): failed", host_object->path, method_name);
		dbus_error_free(&error);
	}

	return result;
}

