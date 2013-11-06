
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/logging.h>
#include <dborb/process.h>
#include <testbus/process.h>
#include <testbus/file.h>

#include "model.h"
#include "host.h"
#include "command.h"

const char *
xni_testbus_host_full_path(const ni_testbus_host_t *host)
{
	static char pathbuf[256];

	snprintf(pathbuf, sizeof(pathbuf), "%s%u", NI_TESTBUS_HOST_BASE_PATH, host->context.id);
	return pathbuf;
}

ni_dbus_object_t *
ni_testbus_host_wrap(ni_dbus_object_t *parent_object, ni_testbus_host_t *host)
{
	return ni_testbus_container_wrap(parent_object, ni_testbus_host_class(), &host->context);
}

ni_testbus_host_t *
ni_testbus_host_unwrap(const ni_dbus_object_t *object, DBusError *error)
{
	ni_testbus_container_t *context;

	if (!ni_dbus_object_get_handle_typecheck(object, ni_testbus_host_class(), error))
		return NULL;

	if (!(context = ni_testbus_container_unwrap(object, error)))
		return NULL;

	return ni_testbus_host_cast(context);
}

void *
ni_objectmodel_get_testbus_host(const ni_dbus_object_t *object, ni_bool_t write_access, DBusError *error)
{
	return ni_testbus_host_unwrap(object, error);
}

/*
 * Record the DBus owner of a host
 */
static void
__ni_testbus_host_set_agent(ni_testbus_host_t *host, const char *owner)
{
	owner = ni_testbus_lookup_wellknown_bus_name(owner);

	ni_debug_wicked("host %s owned by %s", host->context.name, owner);
	ni_string_dup(&host->agent_bus_name, owner);
}

/*
 * Send Host.connected() signal
 */
static void
__ni_testbus_host_signal(ni_dbus_object_t *host_object, const char *signal_name)
{
	/* Send the signal */
	ni_dbus_server_send_signal(ni_dbus_object_get_server(host_object), host_object,
			NI_TESTBUS_HOST_INTERFACE,
			signal_name,
			0, NULL);
}

static inline void
ni_testbus_host_signal_connected(ni_dbus_object_t *host_object)
{
	__ni_testbus_host_signal(host_object, "connected");
}

static inline void
ni_testbus_host_signal_ready(ni_dbus_object_t *host_object)
{
	__ni_testbus_host_signal(host_object, "ready");
}

static inline void
ni_testbus_host_signal_reboot(ni_dbus_object_t *host_object)
{
	__ni_testbus_host_signal(host_object, "rebootRequested");
}

static inline void
ni_testbus_host_signal_shutdown(ni_dbus_object_t *host_object)
{
	__ni_testbus_host_signal(host_object, "shutdownRequested");
}

/*
 * Method delegation
 */
static const ni_dbus_method_t *
__ni_testbus_delegate_method(const ni_dbus_method_t *set_method, const ni_dbus_service_t *item_interface)
{
	const ni_dbus_method_t *item_method;

	if (!(item_method = ni_dbus_service_get_method(item_interface, set_method->name))) {
		ni_error("cannot delegate method %s - method not found in %s", set_method->name, item_interface->name);
		return NULL;
	}
	if (!ni_string_eq(set_method->call_signature, item_method->call_signature)) {
		ni_error("cannot delegate method %s to %s - call signature mismatch", set_method->name, item_interface->name);
		return NULL;
	}
	if (item_method->handler == NULL) {
		ni_error("cannot delegate method %s to %s - no handler function", set_method->name, item_interface->name);
		return NULL;
	}

	return item_method;
}

static ni_bool_t
__ni_testbus_delegate_call(ni_dbus_server_t *server, const char *item_path, const ni_dbus_method_t *item_method,
				unsigned int argc, const ni_dbus_variant_t *argv,
				ni_dbus_message_t *reply, DBusError *error)
{
	ni_dbus_object_t *item_object;

	item_object = ni_dbus_server_get_object(server, item_path);
	if (item_object == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "unable to look up item object %s", item_path);
		return FALSE;
	}
	return item_method->handler(item_object, item_method, argc, argv, NULL, error);
}


static dbus_bool_t
__ni_testbus_context_delegate_hosts(ni_dbus_server_t *server, ni_testbus_container_t *context,
		const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	const ni_dbus_method_t *item_method;
	unsigned int i;

	item_method = __ni_testbus_delegate_method(method, ni_testbus_host_interface());
	if (item_method == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "internal error in Hostset.%s()", method->name);
		return FALSE;
	}

	for (i = 0; i < context->hosts.count; ++i) {
		ni_testbus_host_t *host = context->hosts.data[i];

		if (!__ni_testbus_delegate_call(server, host->context.dbus_object_path, item_method,
					argc, argv, reply, error))
			return FALSE;
	}
	return TRUE;
}

/*
 * Hostlist.createHost(name)
 *
 */
static dbus_bool_t
__ni_Testbus_Hostlist_createHost(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_container_t *context = ni_testbus_global_context();
	ni_dbus_object_t *host_object;
	ni_testbus_host_t *host;
	const char *name;
	int rc;

	if (argc != 1 || !ni_dbus_variant_get_string(&argv[0], &name))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if ((host = ni_testbus_host_new(context, name, &rc)) == NULL) {
		ni_error("unable to create host \"%s\": error %d", name, rc);
		ni_dbus_set_error_from_code(error, rc, "unable to create new host \"%s\"", name);
		return FALSE;
	}

	/* Remember the DBus name of the service owning this object, so that we can
	 * send it messages. */
	__ni_testbus_host_set_agent(host, dbus_message_get_destination(reply));

	/* Register this object */
	host_object = ni_testbus_host_wrap(object, host);
	ni_dbus_message_append_string(reply, host_object->path);

	ni_testbus_host_signal_connected(host_object);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Hostlist, createHost);

/*
 * Hostlist.removeHost(name)
 *
 */
static dbus_bool_t
__ni_Testbus_Hostlist_removeHost(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_container_t *context = ni_testbus_global_context();
	ni_testbus_host_t *host;
	const char *name;

	if (argc != 1 || !ni_dbus_variant_get_string(&argv[0], &name))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if (!(host = ni_testbus_container_get_host_by_name(context, name))) {
		dbus_set_error(error, NI_DBUS_ERROR_NAME_UNKNOWN, "unknown host \"%s\"", name);
		return FALSE;
	}
	ni_testbus_container_remove_host(context, host);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Hostlist, removeHost);

/*
 * Hostlist.reconnect(name, uuid)
 *
 * If a host with the given name exists, and its uuid matches the one presented
 * by the client, return its object path.
 *
 * Otherwise, return an empty string.
 */
static dbus_bool_t
__ni_Testbus_Hostlist_reconnect(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_container_t *context = ni_testbus_global_context();
	const char *name;
	ni_testbus_host_t *host;
	ni_uuid_t uuid;

	if (argc != 2
	 || !ni_dbus_variant_get_string(&argv[0], &name)
	 || !ni_dbus_variant_get_uuid(&argv[1], &uuid))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	host = ni_testbus_container_get_host_by_name(context, name);
	if (host == NULL) {
		int rc;

		if ((host = ni_testbus_host_new(context, name, &rc)) == NULL) {
			ni_dbus_set_error_from_code(error, rc, "unable to create new host \"%s\"", name);
			return FALSE;
		}
		host->uuid = uuid;

		(void) ni_testbus_host_wrap(object, host);
	} else if (!ni_uuid_equal(&host->uuid, &uuid)) {
		ni_debug_wicked("Hostlist.reconnect: cannot reconnect host \"%s\", uuid mismatch", name);
		dbus_set_error(error, NI_DBUS_ERROR_NAME_EXISTS, "host name \"%s\" already taken (uuid mismatch)", name);
		return FALSE;
	} else
	if (host->agent_bus_name != NULL) {
		ni_debug_wicked("Hostlist.reconnect: cannot reconnect host \"%s\", already claimed by other service", name);
		dbus_set_error(error, NI_DBUS_ERROR_NAME_EXISTS, "host name \"%s\" already taken (duplicate registration)", name);
		return FALSE;
	}
	
	/* Remember the DBus name of the service owning this object, so that we can
	 * send it messages. */
	__ni_testbus_host_set_agent(host, dbus_message_get_destination(reply));

	ni_debug_wicked("reconnecting host \"%s\" - object path %s", name, host->context.dbus_object_path);
	ni_dbus_message_append_string(reply, host->context.dbus_object_path);

	ni_testbus_host_signal_connected(object);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Hostlist, reconnect);

/*
 * Hostlist.shutdown
 */
static dbus_bool_t
__ni_Testbus_Hostlist_shutdown(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	return __ni_testbus_context_delegate_hosts(ni_dbus_object_get_server(object),
			ni_testbus_global_context(), method, argc, argv, reply, error);
}

NI_TESTBUS_METHOD_BINDING(Hostlist, shutdown);

/*
 * Hostlist.reboot
 */
static dbus_bool_t
__ni_Testbus_Hostlist_reboot(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	return __ni_testbus_context_delegate_hosts(ni_dbus_object_get_server(object),
			ni_testbus_global_context(), method, argc, argv, reply, error);
}

NI_TESTBUS_METHOD_BINDING(Hostlist, reboot);


static ni_dbus_property_t       __ni_Testbus_Host_properties[] = {
	NI_DBUS_GENERIC_STRING_PROPERTY(testbus_host, name, context.name, RO),
	NI_DBUS_GENERIC_UUID_PROPERTY(testbus_host, uuid, uuid, RO),
	NI_DBUS_GENERIC_BOOL_PROPERTY(testbus_host, ready, ready, RO),
	NI_DBUS_GENERIC_STRING_PROPERTY(testbus_host, agent, agent_bus_name, RO),
	NI_DBUS_GENERIC_STRING_PROPERTY(testbus_host, role, role, RO),
	NI_DBUS_GENERIC_STRING_ARRAY_PROPERTY(testbus_host, capabilities, capabilities, RO),
	{ NULL }
};
NI_TESTBUS_PROPERTIES_BINDING(Host);


/*
 * Send Host.processScheduled() signal, passing the process parameters as a dict argument
 */
static ni_bool_t
ni_testbus_host_signal_process_scheduled(ni_dbus_object_t *host_object, ni_dbus_object_t *process_object, ni_testbus_process_t *proc)
{
	ni_dbus_variant_t argv[2];
	ni_process_t *pi;
	ni_bool_t rv = FALSE;

	/* Create a process object */
	pi = ni_process_new_ext(&proc->argv, &proc->context.env.vars);
	if (pi == NULL) {
		ni_error("unable to create process object");
		return FALSE;
	}

	ni_dbus_variant_vector_init(argv, 2);

	if (!ni_testbus_process_serialize(pi, &argv[0])) {
		ni_error("unable to serialize process instance");
		goto out;
	}
	ni_dbus_dict_add_string(&argv[0], "object-path", process_object->path);

	if (!ni_testbus_file_array_serialize(&proc->context.files, &argv[1])) {
		ni_error("unable to serialize fileset");
		goto out;
	}

	/* Send the signal */
	ni_dbus_server_send_signal(ni_dbus_object_get_server(host_object), host_object,
			NI_TESTBUS_HOST_INTERFACE,
			"processScheduled",
			2, argv);
	rv = TRUE;

out:
	ni_process_free(pi);
	ni_dbus_variant_vector_destroy(argv, 2);
	return rv;
}

/*
 * Host.run(object-path)
 *
 * object-path is supposed to refer to a command object
 */
static dbus_bool_t
__ni_Testbus_Host_run(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_host_t *host;
	ni_dbus_object_t *root_object, *command_object = NULL, *process_object;
	const char *command_path = "";
	ni_testbus_command_t *cmd;
	ni_testbus_process_t *proc;

	if (!(host = ni_testbus_host_unwrap(object, error)))
		return FALSE;

	if (argc != 1 || !ni_dbus_variant_get_string(&argv[0], &command_path))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	root_object = ni_dbus_server_get_root_object(ni_dbus_object_get_server(object));
	if (root_object)
		command_object = ni_dbus_object_lookup(root_object, command_path);
	if (command_object == NULL) {
		dbus_set_error(error, NI_DBUS_ERROR_NAME_UNKNOWN,
				"unable to look up dbus object %s",
				command_path);
		return FALSE;
	}

	if (!(cmd = ni_testbus_command_unwrap(command_object, error)))
		return FALSE;

	proc = ni_testbus_process_new(&host->context, cmd);
	ni_testbus_process_apply_context(proc, &cmd->context);
	ni_testbus_process_apply_context(proc, &host->context);

	/* Create the DBus object for this process */
	process_object = ni_testbus_process_wrap(object, proc);

	/* For all the output files attached to the command, we create
	 * a new file object relative to the process. Otherwise, the
	 * process would store its output in the Command's file - which
	 * is bad if you run several intances of the same command on
	 * different hosts. */
	if (ni_testbus_process_finalize(proc)) {
		unsigned int i;

		for (i = 0; i < proc->context.files.count; ++i) {
			ni_testbus_file_t *file = proc->context.files.data[i];

			if ((file->mode & NI_TESTBUS_FILE_WRITE)
			 && file->object_path == NULL)
				ni_testbus_file_wrap(process_object, file);
			 
		}
	}

	/* Rather than executing it locally (on the master host) send it
	 * to the agent and make it execute there.
	 *
	 * We do not place any calls to the agents from the master - this
	 * could block if the agent is currently unreachable.
	 * Instead, we broadcast a Host.processScheduled() signal, giving
	 * the object path of the newly created process as an argument.
	 * The agent is then supposed to retrieve the process and its
	 * properties, and run it locally.
	 */
	ni_testbus_host_signal_process_scheduled(object, process_object, proc);

	ni_debug_wicked("created process object %s", process_object->path);
	ni_dbus_message_append_string(reply, process_object->path);
	return TRUE;
}

static dbus_bool_t
__ni_Testbus_Host_run_ex(ni_dbus_object_t *object, const ni_dbus_method_call_ctx_t *ctx,
		ni_dbus_message_t *reply, DBusError *error)
{
	return __ni_Testbus_Host_run(object,
			ctx->method, ctx->argc, ctx->argv,
			reply, error);
}

NI_TESTBUS_EXT_METHOD_BINDING(Host, run);

/*
 * Host.addCapability(string)
 */
static dbus_bool_t
__ni_Testbus_Host_addCapability(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_host_t *host;
	const char *capability = "";

	if (!(host = ni_testbus_host_unwrap(object, error)))
		return FALSE;

	if (argc != 1 || !ni_dbus_variant_get_string(&argv[0], &capability))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	ni_testbus_host_add_capability(host, capability);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Host, addCapability);

/*
 * Host.shutdown()
 */
static dbus_bool_t
__ni_Testbus_Host_shutdown(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	/* We don't need the host, we just check to make sure it *is* a host */
	if (ni_testbus_host_unwrap(object, error) == NULL)
		return FALSE;

	if (argc != 0)
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	ni_testbus_host_signal_shutdown(object);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Host, shutdown);

/*
 * Host.reboot()
 */
static dbus_bool_t
__ni_Testbus_Host_reboot(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	/* We don't need the host, we just check to make sure it *is* a host */
	if (ni_testbus_host_unwrap(object, error) == NULL)
		return FALSE;

	if (argc != 0)
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	ni_testbus_host_signal_reboot(object);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Host, reboot);

/*
 * Hostset.addHost(name, host-object-path)
 */
static dbus_bool_t
__ni_Testbus_Hostset_addHost(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_container_t *context;
	ni_dbus_object_t *root_object, *host_object = NULL;
	const char *name, *host_object_path;
	ni_testbus_host_t *host;

	if ((context = ni_testbus_container_unwrap(object, error)) == NULL)
		return FALSE;

	if (argc != 2
	 || !ni_dbus_variant_get_string(&argv[0], &name)
	 || !ni_dbus_variant_get_string(&argv[1], &host_object_path))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if (!ni_testbus_identifier_valid(name, error))
		return FALSE;

	if (ni_testbus_container_get_host_by_role(context, name) != NULL) {
		dbus_set_error(error, NI_DBUS_ERROR_NAME_EXISTS,
				"you already have a host by this role");
		return FALSE;
	}

	{
		ni_dbus_server_t *server = ni_dbus_object_get_server(object);

		root_object = NULL;
		if (server)
			root_object = ni_dbus_server_get_root_object(server);
		if (root_object)
			host_object = ni_dbus_object_lookup(root_object, host_object_path);
	}

	if (host_object == NULL) {
		dbus_set_error(error, NI_DBUS_ERROR_NAME_UNKNOWN,
				"unknown host object path %s", host_object_path);
		return FALSE;
	}

	if (!ni_dbus_object_isa(host_object, ni_testbus_host_class())) {
		dbus_set_error(error, NI_DBUS_ERROR_NOT_COMPATIBLE,
				"object %s is not a host object", host_object_path);
		return FALSE;
	}

	if (!(host = ni_testbus_host_unwrap(host_object, error)))
		return FALSE;

	if (!ni_testbus_host_set_role(host, name, context)) {
		dbus_set_error(error, NI_DBUS_ERROR_IN_USE, "host already in use");
		return FALSE;
	}

	ni_testbus_container_add_host(context, host);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Hostset, addHost);

static dbus_bool_t
__ni_Testbus_Hostset_delegate(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_container_t *context;

	if ((context = ni_testbus_container_unwrap(object, error)) == NULL)
		return FALSE;

	return __ni_testbus_context_delegate_hosts(ni_dbus_object_get_server(object), context,
						method, argc, argv, reply, error);
}

/*
 * Hostset.shutdown
 */
static dbus_bool_t
__ni_Testbus_Hostset_shutdown(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	return __ni_Testbus_Hostset_delegate(object, method, argc, argv, reply, error);
}

NI_TESTBUS_METHOD_BINDING(Hostset, shutdown);

/*
 * Hostset.reboot
 */
static dbus_bool_t
__ni_Testbus_Hostset_reboot(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	return __ni_Testbus_Hostset_delegate(object, method, argc, argv, reply, error);
}

NI_TESTBUS_METHOD_BINDING(Hostset, reboot);

/*
 * Handle signals from agent
 */
static void
__ni_testbus_host_agent_signal(ni_dbus_connection_t *connection, ni_dbus_message_t *msg, void *user_data)
{
	ni_dbus_server_t *server = user_data;
	const char *sender_name = dbus_message_get_sender(msg);
	const char *signal_name = dbus_message_get_member(msg);

	ni_debug_wicked("received signal %s.%s() from %s",
			dbus_message_get_interface(msg),
			signal_name, sender_name);

	if (ni_string_eq(signal_name, "ready")) {
		ni_testbus_host_t *host;

		sender_name = ni_testbus_lookup_wellknown_bus_name(sender_name);
		if ((host = ni_testbus_container_find_agent_host(ni_testbus_global_context(), sender_name)) != NULL) {
			ni_testbus_host_agent_ready(host);
			if (host->context.dbus_object_path) {
				ni_dbus_object_t *host_object;

				host_object = ni_dbus_server_get_object(server,
						host->context.dbus_object_path);
				if (host_object)
					ni_testbus_host_signal_ready(host_object);
			}
		}
	}

}


void
ni_testbus_create_static_objects_host(ni_dbus_server_t *server)
{
	ni_objectmodel_create_object(server, NI_TESTBUS_HOSTLIST_PATH, ni_testbus_hostlist_class(), NULL);

	ni_dbus_server_add_signal_handler(server,
			NULL,				/* any sender - should be Testbus.Agent* */
			NULL,				/* any object path */
			NI_TESTBUS_AGENT_INTERFACE,	/* interface */
			__ni_testbus_host_agent_signal,
			server);
}

void
ni_testbus_bind_builtin_host(void)
{
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Hostlist_createHost_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Hostlist_removeHost_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Hostlist_reconnect_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Hostlist_shutdown_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Hostlist_reboot_binding);

	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Host_run_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Host_addCapability_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Host_shutdown_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Host_reboot_binding);
	ni_dbus_objectmodel_bind_properties(&__ni_Testbus_Host_Properties_binding);

	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Hostset_addHost_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Hostset_shutdown_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Hostset_reboot_binding);
}
