/*
 * DBus encapsulation for network interfaces
 *
 * Copyright (C) 2011-2012 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>

#include <dborb/logging.h>
#include <dborb/dbus-errors.h>
#include <dborb/xml.h>
#include "dbus-common.h"
#include "appconfig.h"
#include "debug.h"

static unsigned int		ni_dbus_lookup_count;
static const ni_dbus_lookup_t **ni_dbus_lookup_list;

void
ni_dbus_register_lookup(ni_dbus_lookup_t *ns)
{
	if ((ni_dbus_lookup_count % 8) == 0)
		ni_dbus_lookup_list = realloc(ni_dbus_lookup_list,
				(ni_dbus_lookup_count + 8) * sizeof(ni_dbus_lookup_list[0]));
	ni_dbus_lookup_list[ni_dbus_lookup_count++] = ns;
}

const ni_dbus_lookup_t *
ni_dbus_get_lookup(const char *name)
{
	unsigned int i;

	for (i = 0; i < ni_dbus_lookup_count; ++i) {
		const ni_dbus_lookup_t *ns;

		ns = ni_dbus_lookup_list[i];
		if (ni_string_eq(ns->name, name))
			return ns;
	}
	return NULL;
}

/*
 * Register all naming services specified in the config file.
 * These naming services are supposed to be provided by shared libraries.
 * The symbol specified by the C binding element must refer to a
 * ni_dbus_lookup_t object.
 */
void
ni_dbus_register_dynamic_lookups(void)
{
	ni_config_t *config = ni_global.config;
	ni_extension_t *ex;

	ni_assert(config);
	for (ex = config->ns_extensions; ex; ex = ex->next) {
		ni_c_binding_t *binding;
		void *addr;

		for (binding = ex->c_bindings; binding; binding = binding->next) {
			if ((addr = ni_c_binding_get_address(binding)) == NULL) {
				ni_error("cannot bind %s name service - invalid C binding",
						binding->name);
				continue;
			}

			ni_debug_objectmodel("trying to bind netif naming service \"%s\"", binding->name);
			ni_dbus_register_lookup((ni_dbus_lookup_t *) addr);
		}
	}
}

ni_dbus_object_t *
ni_dbus_lookup_by_attrs(ni_dbus_object_t *list_object, const ni_dbus_lookup_t *ns, const ni_var_array_t *attrs)
{
	ni_dbus_object_t *obj;

	if (ns->lookup_by_attrs)
		return ns->lookup_by_attrs(ns, attrs);

	if (ns->match_attr == NULL)
		return NULL;

	for (obj = list_object->children; obj; obj = obj->next) {
		ni_bool_t match = TRUE;
		ni_var_t *ap;
		unsigned int i;

		for (i = 0, ap = attrs->data; match && i < attrs->count; ++i, ++ap)
			match = ns->match_attr(obj, ap->name, ap->value);
		if (match) {
			ni_debug_dbus("%s: found %s", __func__, obj->path);
			return obj;
		}
	}
	return NULL;
}

/*
 * Provide all possible descriptions of a device.
 */
xml_node_t *
ni_dbus_object_get_names(const ni_dbus_object_t *object)
{
	xml_node_t *result;
	unsigned int i;
	ni_bool_t ok = FALSE;

	result = xml_node_new(NULL, NULL);
	for (i = 0; i < ni_dbus_lookup_count; ++i) {
		const ni_dbus_lookup_t *ns;

		ns = ni_dbus_lookup_list[i];
		if (ns->describe && ns->describe(ns, object, result))
			ok = TRUE;
	}

	if (!ok) {
		xml_node_free(result);
		result = NULL;
	}

	return result;
}
