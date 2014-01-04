/*
 * dbus service extensions
 *
 * Copyright (C) 2011-2014 Olaf Kirch <okir@suse.de>
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
#include <dborb/process.h>
#include "util_priv.h"
#include "dbus-common.h"
#include "dbus-connection.h"
#include "xml-schema.h"
#include "appconfig.h"
#include "debug.h"

/*
 * Expand the environment of an extension
 * This should probably go with the objectmodel code.
 */
static int
ni_dbus_extension_expand_environment(const ni_dbus_object_t *object, const ni_var_array_t *env, ni_process_t *process)
{
	const ni_var_t *var;
	unsigned int i;

	for (i = 0, var = env->data; i < env->count; ++i, ++var) {
		ni_dbus_variant_t variant = NI_DBUS_VARIANT_INIT;
		const char *value = var->value;

		if (!strcmp(value, "$object-path")) {
			value = object->path;
		} else if (!strncmp(value, "$property:", 10)) {
			if (ni_dbus_object_get_property(object, value + 10, NULL, &variant)) {
				value = ni_dbus_variant_sprint(&variant);
			}
		} else if (value[0] == '$') {
			ni_error("%s: unable to expand environment variable %s=\"%s\"",
					object->path, var->name, var->value);
			return -1;
		}

		ni_debug_dbus("%s: expanded %s=%s -> \"%s\"", object->path, var->name, var->value, value);
		ni_process_setenv(process, var->name, value);

		ni_dbus_variant_destroy(&variant);
	}

	return 0;
}

/*
 * Write dbus message to a temporary file
 */
static char *
__ni_dbus_extension_write_message(ni_dbus_message_t *msg, const ni_dbus_method_t *method, ni_tempstate_t *temp_state)
{
	ni_dbus_variant_t argv[16];
	char *tempname = NULL;
	xml_node_t *xmlnode;
	int argc = 0;
	FILE *fp;

	/* Deserialize dbus message */
	memset(argv, 0, sizeof(argv));
	argc = ni_dbus_message_get_args_variants(msg, argv, 16);
	if (argc < 0)
		return NULL;

	xmlnode = ni_dbus_xml_deserialize_arguments(method, argc, argv, NULL, temp_state);

	while (argc--)
		ni_dbus_variant_destroy(&argv[argc]);

	if (xmlnode == NULL) {
		ni_error("%s: unable to build XML from arguments", method->name);
		return NULL;
	}

	if ((fp = ni_mkstemp(&tempname)) == NULL) {
		ni_error("%s: unable to create tempfile for script arguments", __func__);
	} else {
		/* Add file to tempstate; it will be deleted when we destroy the process handle */
		ni_tempstate_add_file(temp_state, tempname);
		if (xml_node_print(xmlnode, fp) < 0) {
			ni_error("%s: unable to store message arguments in file", method->name);
			ni_string_free(&tempname); /* tempname is NULL after this */
		}

		fclose(fp);
	}

	xml_node_free(xmlnode);
	return tempname;
}

static char *
__ni_dbus_extension_empty_tempfile(ni_tempstate_t *temp_state)
{
	char *tempname = NULL;
	FILE *fp;

	if ((fp = ni_mkstemp(&tempname)) == NULL) {
		ni_error("%s: unable to create tempfile for script arguments", __func__);
		return NULL;
	}

	fclose(fp);

	/* Add file to tempstate; it will be deleted when we destroy the process handle */
	ni_tempstate_add_file(temp_state, tempname);
	return tempname;
}

dbus_bool_t
ni_dbus_extension_extension_call(ni_dbus_connection_t *connection,
				ni_dbus_object_t *object, const ni_dbus_method_t *method,
				ni_dbus_message_t *call)
{
	DBusError error = DBUS_ERROR_INIT;
	const char *interface = dbus_message_get_interface(call);
	ni_tempstate_t *temp_state = NULL;
	ni_extension_t *extension;
	ni_shellcmd_t *command;
	ni_process_t *process;
	char *tempname = NULL;

	NI_TRACE_ENTER_ARGS("object=%s, interface=%s, method=%s", object->path, interface, method->name);

	extension = ni_config_find_extension(interface);
	if (extension == NULL) {
		dbus_set_error(&error, DBUS_ERROR_SERVICE_UNKNOWN, "%s: no/unknown interface %s",
				__func__, interface);
		ni_dbus_connection_send_error(connection, call, &error);
		return FALSE;
	}

	if ((command = ni_extension_script_find(extension, method->name)) == NULL) {
		dbus_set_error(&error, DBUS_ERROR_FAILED, "%s: no/unknown extension method %s",
				__func__, method->name);
		ni_dbus_connection_send_error(connection, call, &error);
		return FALSE;
	}

	ni_debug_extension("preparing to run extension script \"%s\"", command->command);

	/* Create an instance of this command */
	process = ni_process_new_shellcmd(command);

	ni_dbus_extension_expand_environment(object, &extension->environment, process);
	temp_state = ni_process_tempstate(process);

	/* Build the argument blob and store it in a file */
	tempname = __ni_dbus_extension_write_message(call, method, temp_state);
	if (tempname != NULL) {
		ni_process_setenv(process, "WICKED_ARGFILE", tempname);
		ni_string_free(&tempname);
	} else {
		dbus_set_error(&error, DBUS_ERROR_INVALID_ARGS,
				"Bad arguments in call to object %s, %s.%s",
				object->path, interface, method->name);
		goto send_error;
	}

	/* Create empty reply for script return data */
	tempname = __ni_dbus_extension_empty_tempfile(temp_state);
	if (tempname != NULL) {
		ni_process_setenv(process, "WICKED_RETFILE", tempname);
		ni_string_free(&tempname);
	} else {
		goto general_failure;
	}

	/* Run the process */
	if (ni_dbus_async_server_call_run_command(connection, object, method, call, process) < 0) {
		ni_error("%s: error executing method %s", __func__, method->name);
		dbus_set_error(&error, DBUS_ERROR_FAILED, "%s: error executing method %s",
				__func__, method->name);
		ni_dbus_connection_send_error(connection, call, &error);
		ni_process_free(process);
		return FALSE;
	}

	return TRUE;

general_failure:
	dbus_set_error(&error, DBUS_ERROR_FAILED, "%s - general failure when executing method",
			method->name);

send_error:
	ni_dbus_connection_send_error(connection, call, &error);

	if (process)
		ni_process_free(process);

	return FALSE;
}

static dbus_bool_t
ni_dbus_extension_extension_completion(ni_dbus_connection_t *connection, const ni_dbus_method_t *method,
				ni_dbus_message_t *call, const ni_process_t *process)
{
	const char *interface_name = dbus_message_get_interface(call);
	DBusError error = DBUS_ERROR_INIT;
	ni_dbus_message_t *reply;
	const char *filename;
	xml_document_t *doc = NULL;

	if ((filename = ni_process_getenv(process, "WICKED_RETFILE")) != NULL) {
		if (!(doc = xml_document_read(filename)))
			ni_error("%s.%s: failed to parse return data",
					interface_name, method->name);
	}

	if (ni_process_exit_status_okay(process)) {
		ni_dbus_variant_t result = NI_DBUS_VARIANT_INIT;
		xml_node_t *retnode = NULL;
		int nres;

		/* if the method returns anything, read it from the response file
		 * and encode it. */
		if (doc == NULL
		 || (retnode = xml_node_get_child(xml_document_root(doc), "return")) == NULL) {
			nres = 0;
		} else if ((nres = ni_dbus_serialize_return(method, &result, retnode)) < 0) {
			dbus_set_error(&error, NI_DBUS_ERROR_CANNOT_MARSHAL,
					"%s.%s: unable to serialize returned data",
					interface_name, method->name);
			ni_dbus_variant_destroy(&result);
			goto send_error;
		}

		/* Build the response message */
		reply = dbus_message_new_method_return(call);
		if (!ni_dbus_message_serialize_variants(reply, nres, &result, &error)) {
			ni_dbus_variant_destroy(&result);
			dbus_message_unref(reply);
			goto send_error;
		}
		ni_dbus_variant_destroy(&result);
	} else {
		xml_node_t *errnode = NULL;

		if (doc != NULL)
			errnode = xml_node_get_child(xml_document_root(doc), "error");

		if (errnode)
			ni_dbus_serialize_error(&error, errnode);
		else
			dbus_set_error(&error, DBUS_ERROR_FAILED, "dbus extension script returns error");

send_error:
		reply = dbus_message_new_error(call, error.name, error.message);
	}

	if (ni_dbus_connection_send_message(connection, reply) < 0)
		ni_error("unable to send reply (out of memory)");

	dbus_message_unref(reply);
	return TRUE;
}

/*
 * Bind extension scripts to the interface functions they are specified for.
 */
void
ni_dbus_service_bind_extension(const ni_dbus_service_t *service, const ni_extension_t *extension)
{
	const ni_dbus_method_t *method;
	const ni_c_binding_t *binding;

	for (method = service->methods; method && method->name != NULL; ++method) {
		ni_dbus_method_t *mod_method = (ni_dbus_method_t *) method;

		if (method->handler != NULL)
			continue;
		if (ni_extension_script_find(extension, method->name) != NULL) {
			ni_debug_dbus("binding method %s.%s to external command",
					service->name, method->name);
			mod_method->async_handler = ni_dbus_extension_extension_call;
			mod_method->async_completion = ni_dbus_extension_extension_completion;
		} else
		if ((binding = ni_extension_find_c_binding(extension, method->name)) != NULL) {
			void *addr;

			if ((addr = ni_c_binding_get_address(binding)) == NULL) {
				ni_error("cannot bind method %s.%s - invalid C binding",
						service->name, method->name);
				continue;
			}

			ni_debug_dbus("binding method %s.%s to builtin %s",
					service->name, method->name, binding->symbol);
			mod_method->handler = addr;
		}
	}

	/* Bind the properties table if we have one */
	if ((binding = ni_extension_find_c_binding(extension, "__properties")) != NULL) {
		ni_dbus_service_t *mod_service = ((ni_dbus_service_t *) service);
		void *addr;

		if ((addr = ni_c_binding_get_address(binding)) == NULL) {
			ni_error("cannot bind %s properties - invalid C binding",
					service->name);
		} else {
			mod_service->properties = addr;
		}
	}

}
