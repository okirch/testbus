
/*
 * Handle global configuration for netinfo
 *
 * Copyright (C) 2010-2012 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <dlfcn.h>

#include <dborb/util.h>
#include <dborb/netinfo.h>
#include <dborb/xpath.h>
#include <dborb/dbus.h>
#include "util_priv.h"
#include "appconfig.h"

#define DEFAULT_PIDDIR		"/var/run/testbus"
#define DEFAULT_STATEDIR	"/var/lib/testbus"

static void		ni_config_parse_fslocation(ni_config_fslocation_t *, xml_node_t *);
static ni_bool_t	ni_config_parse_objectmodel_extension(ni_config_t *, xml_node_t *);
static ni_bool_t	ni_config_parse_one_extension(ni_extension_t **, xml_node_t *);
static ni_bool_t	ni_config_parse_extension(ni_extension_t *, xml_node_t *);
static ni_c_binding_t *	ni_c_binding_new(ni_c_binding_t **, const char *name, const char *lib, const char *symbol);
static void		ni_config_fslocation_init(ni_config_fslocation_t *, const char *path, unsigned int mode);
static void		ni_config_fslocation_destroy(ni_config_fslocation_t *);
static const char *	ni_config_build_include(const char *, const char *);

/*
 * Create an empty config object
 */
ni_config_t *
ni_config_new()
{
	ni_config_t *conf;

	conf = calloc(1, sizeof(*conf));

	conf->recv_max = 64 * 1024;

	ni_config_fslocation_init(&conf->piddir, DEFAULT_PIDDIR, 0755);
	ni_config_fslocation_init(&conf->statedir, DEFAULT_STATEDIR, 0755);

	return conf;
}

void
ni_config_free(ni_config_t *conf)
{
	ni_string_free(&conf->dbus_name);
	ni_string_free(&conf->dbus_type);
	ni_string_free(&conf->dbus_socket);
	ni_config_fslocation_destroy(&conf->piddir);
	ni_config_fslocation_destroy(&conf->statedir);
	free(conf);
}

ni_bool_t
__ni_config_parse(ni_config_t *conf, const char *filename, ni_init_appdata_callback_t *cb, void *appdata)
{
	xml_document_t *doc;
	xml_node_t *node, *child;

	ni_debug_wicked("Reading config file %s", filename);
	doc = xml_document_read(filename);
	if (!doc) {
		ni_error("%s: error parsing configuration file", filename);
		goto failed;
	}

	node = xml_node_get_child(doc->root, "config");
	if (!node) {
		ni_error("%s: no <config> element", filename);
		goto failed;
	}

	/* Loop over all elements in the config file */
	for (child = node->children; child; child = child->next) {
		if (strcmp(child->name, "include") == 0) {
			const char *attrval, *path;

			if ((attrval = xml_node_get_attr(child, "name")) == NULL) {
				ni_error("%s: <include> element lacks filename", xml_node_location(child));
				goto failed;
			}
			if (!(path = ni_config_build_include(filename, attrval)))
				goto failed;
			if (!__ni_config_parse(conf, path, cb, appdata))
				goto failed;
		} else
		if (strcmp(child->name, "piddir") == 0) {
			ni_config_parse_fslocation(&conf->piddir, child);
		} else
		if (strcmp(child->name, "statedir") == 0) {
			ni_config_parse_fslocation(&conf->statedir, child);
		} else
		if (strcmp(child->name, "dbus") == 0) {
			const char *attrval;

			if ((attrval = xml_node_get_attr(child, "name")) != NULL)
				ni_string_dup(&conf->dbus_name, attrval);
			if ((attrval = xml_node_get_attr(child, "type")) != NULL)
				ni_string_dup(&conf->dbus_type, attrval);
			if ((attrval = xml_node_get_attr(child, "socket")) != NULL)
				ni_string_dup(&conf->dbus_socket, attrval);
		} else 
		if (strcmp(child->name, "schema") == 0) {
			const char *attrval;

			if ((attrval = xml_node_get_attr(child, "name")) != NULL)
				ni_string_dup(&conf->dbus_xml_schema_file, attrval);
		} else
		if (strcmp(child->name, "extension") == 0) {
			if (!ni_config_parse_objectmodel_extension(conf, child))
				goto failed;
		} else
		if (strcmp(child->name, "debug") == 0) {
			ni_enable_debug(child->cdata);
		} else
		if (cb != NULL) {
			if (!cb(appdata, child))
				goto failed;
		}
	}

	if (conf->backupdir.path == NULL) {
		char pathname[PATH_MAX];

		snprintf(pathname, sizeof(pathname), "%s/backup", conf->statedir.path);
		ni_config_fslocation_init(&conf->backupdir, pathname, 0700);
	}

	xml_document_free(doc);
	return TRUE;

failed:
	if (doc)
		xml_document_free(doc);
	return FALSE;
}

ni_config_t *
ni_config_parse(const char *filename, ni_init_appdata_callback_t *cb, void *appdata)
{
	ni_config_t *conf;

	conf = ni_config_new();
	if (!__ni_config_parse(conf, filename, cb, appdata)) {
		ni_config_free(conf);
		return NULL;
	}

	return conf;
}

const char *
ni_config_build_include(const char *parent_filename, const char *incl_filename)
{
	char fullname[PATH_MAX + 1];

	if (incl_filename[0] != '/') {
		unsigned int i;

		i = strlen(parent_filename);
		if (i >= PATH_MAX)
			goto too_long;
		strcpy(fullname, parent_filename);

		while (i && fullname[i-1] != '/')
			--i;
		fullname[i] = '\0';

		if (i + strlen(incl_filename) >= PATH_MAX)
			goto too_long;
		strcpy(&fullname[i], incl_filename);
		incl_filename = fullname;
	}
	return incl_filename;

too_long:
	ni_error("unable to include \"%s\" - path too long", incl_filename);
	return NULL;
}

void
ni_config_parse_fslocation(ni_config_fslocation_t *fsloc, xml_node_t *node)
{
	const char *attrval;

	if ((attrval = xml_node_get_attr(node, "path")) != NULL)
		ni_string_dup(&fsloc->path, attrval);
	if ((attrval = xml_node_get_attr(node, "mode")) != NULL)
		ni_parse_uint(attrval, &fsloc->mode, 8);
}

/*
 * Object model extensions let you implement parts of a dbus interface separately
 * from the main wicked body of code; either through a shared library or an
 * external command/shell script
 *
 * <extension type="dbus-service" interface="org.opensuse.Network.foobar">
 *  <action name="dbusMethodName" command="/some/shell/scripts some-args"/>
 *  <builtin name="dbusOtherMethodName" library="/usr/lib/libfoo.so" symbol="c_method_impl_name"/>
 *
 *  <putenv name="WICKED_OBJECT_PATH" value="$object-path"/>
 *  <putenv name="WICKED_INTERFACE_NAME" value="$property:name"/>
 *  <putenv name="WICKED_INTERFACE_INDEX" value="$property:index"/>
 * </extension>
 */
ni_bool_t
ni_config_parse_objectmodel_extension(ni_config_t *conf, xml_node_t *node)
{
	const char *type;

	if (!(type = xml_node_get_attr(node, "type"))) {
		ni_error("%s: <%s> element lacks type attribute",
				node->name, xml_node_location(node));
		return FALSE;
	}

	if (strcmp(type, "dbus-service") == 0)
		return ni_config_parse_one_extension(&conf->dbus_extensions, node);

	if (strcmp(type, "dbus-lookup") == 0)
		return ni_config_parse_one_extension(&conf->ns_extensions, node);

	ni_error("%s: <%s> element specifies unknown extension type \"%s\"",
				node->name, xml_node_location(node), type);
	return FALSE;
}

ni_bool_t
ni_config_parse_one_extension(ni_extension_t **list, xml_node_t *node)
{
	ni_extension_t *ex;
	const char *name;

	if (!(name = xml_node_get_attr(node, "interface"))) {
		ni_error("%s: <%s> element lacks interface attribute",
				node->name, xml_node_location(node));
		return FALSE;
	}

	ex = ni_extension_new(list, name);

	return ni_config_parse_extension(ex, node);
}

static ni_bool_t
ni_config_parse_extension(ni_extension_t *ex, xml_node_t *node)
{
	xml_node_t *child;

	for (child = node->children; child; child = child->next) {
		if (!strcmp(child->name, "action") || !strcmp(child->name, "script")) {
			const char *name, *command;

			if (!(name = xml_node_get_attr(child, "name"))) {
				ni_error("action element without name attribute");
				return FALSE;
			}
			if (!(command = xml_node_get_attr(child, "command"))) {
				ni_error("action element without command attribute");
				return FALSE;
			}

			if (!ni_extension_script_new(ex, name, command))
				return FALSE;
		} else
		if (!strcmp(child->name, "builtin")) {
			const char *name, *library, *symbol;

			if (!(name = xml_node_get_attr(child, "name"))) {
				ni_error("builtin element without name attribute");
				return FALSE;
			}
			if (!(symbol = xml_node_get_attr(child, "symbol"))) {
				ni_error("action element without command attribute");
				return FALSE;
			}
			library = xml_node_get_attr(child, "library");

			ni_c_binding_new(&ex->c_bindings, name, library, symbol);
		} else
		if (!strcmp(child->name, "putenv")) {
			const char *name, *value;

			if (!(name = xml_node_get_attr(child, "name"))) {
				ni_error("%s: <putenv> element without name attribute",
						xml_node_location(child));
				return FALSE;
			}
			value = xml_node_get_attr(child, "value");
			ni_var_array_set(&ex->environment, name, value);
		}
	}

	return TRUE;
}

/*
 * Extension handling
 */
ni_extension_t *
ni_config_find_extension(const char *interface)
{
	return ni_extension_list_find(ni_global.config->dbus_extensions, interface);
}

/*
 * Handle methods implemented via C bindings
 */
static ni_c_binding_t *
ni_c_binding_new(ni_c_binding_t **list, const char *name, const char *library, const char *symbol)
{
	ni_c_binding_t *binding, **pos;

	for (pos = list; (binding = *pos) != NULL; pos = &binding->next)
		;

	binding = xcalloc(1, sizeof(*binding));
	ni_string_dup(&binding->name, name);
	ni_string_dup(&binding->library, library);
	ni_string_dup(&binding->symbol, symbol);

	*pos = binding;
	return binding;
}

void
ni_c_binding_free(ni_c_binding_t *binding)
{
	ni_string_free(&binding->name);
	ni_string_free(&binding->library);
	ni_string_free(&binding->symbol);
	free(binding);
}

void *
ni_c_binding_get_address(const ni_c_binding_t *binding)
{
	void *handle;
	void *addr;

	handle = dlopen(binding->library, RTLD_LAZY);
	if (handle == NULL) {
		ni_error("invalid binding for %s - cannot dlopen(%s): %s",
				binding->name, binding->library?: "<main>", dlerror());
		return NULL;
	}

	addr = dlsym(handle, binding->symbol);
	dlclose(handle);

	if (addr == NULL) {
		ni_error("invalid binding for %s - no such symbol in %s: %s",
				binding->name, binding->library?: "<main>", binding->symbol);
		return NULL;
	}

	return addr;
}

void
ni_config_fslocation_init(ni_config_fslocation_t *loc, const char *path, unsigned int mode)
{
	memset(loc, 0, sizeof(*loc));
	ni_string_dup(&loc->path, path);
	loc->mode = mode;
}

void
ni_config_fslocation_destroy(ni_config_fslocation_t *loc)
{
	ni_string_free(&loc->path);
	memset(loc, 0, sizeof(*loc));
}
