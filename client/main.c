/*
 * Copyright (C) 2010-2014 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <mcheck.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>

#include <dborb/netinfo.h>
#include <dborb/logging.h>
#include <dborb/xml.h>
#include <dborb/buffer.h>
#include <dborb/process.h>
#include <dborb/dbus-errors.h>
#include <dborb/dbus-model.h>
#include <testbus/model.h>
#include <testbus/client.h>
#include <testbus/process.h>
#include <testbus/monitor.h>

enum {
	OPT_HELP,
	OPT_VERSION,
	OPT_CONFIGFILE,
	OPT_DEBUG,
	OPT_QUIET,
	OPT_LOG_LEVEL,
	OPT_LOG_TARGET,
};

static struct option	options[] = {
	/* common */
	{ "help",		no_argument,		NULL,	OPT_HELP },
	{ "version",		no_argument,		NULL,	OPT_VERSION },
	{ "config",		required_argument,	NULL,	OPT_CONFIGFILE },
	{ "debug",		required_argument,	NULL,	OPT_DEBUG },
	{ "log-level",		required_argument,	NULL,	OPT_LOG_LEVEL },
	{ "log-target",		required_argument,	NULL,	OPT_LOG_TARGET },

	{ NULL }
};

typedef int		client_command_handler_t(int, char **);

struct client_command {
	const char *	name;
	client_command_handler_t *handler;
	const char *	description;
};

static const char *	program_name;
static const char *	opt_log_level;
static const char *	opt_log_target;
static ni_bool_t	opt_quiet;

static client_command_handler_t *get_client_command(const char *);
static void		help_client_commands(void);

static ni_buffer_t *	ni_testbus_read_local_file(const char *);
static long		ni_testbus_write_local_file(const char *, const ni_buffer_t *);

/* XXX rename these */
static int		__do_claim_host_busywait(const ni_testbus_client_timeout_t *);
static void		__do_claim_host_timedout(const ni_testbus_client_timeout_t *);

int
main(int argc, char **argv)
{
	char *cmd;
	client_command_handler_t *handler;
	int c;

	mtrace();

	program_name = ni_basename(argv[0]);
	while ((c = getopt_long(argc, argv, "+", options, NULL)) != EOF) {
		switch (c) {
		case OPT_HELP:
		default:
		usage:
			fprintf(stderr,
				"testbus-client [options] cmd path\n"
				"This command understands the following options\n"
				"  --help\n"
				"  --version\n"
				"  --config filename\n"
				"        Use alternative configuration file.\n"
				"  --log-target target\n"
				"        Set log destination to <stderr|syslog>.\n"
				"  --log-level level\n"
				"        Set log level to <error|warning|notice|info|debug>.\n"
				"  --debug facility\n"
				"        Enable debugging for debug <facility>.\n"
				"        Use '--debug help' for a list of facilities.\n"
				"  --quiet\n"
				"        Suppress progress messages when waiting, and other output\n"
				"        mainly useful for interactive use.\n"
				"\n"
				"Supported commands:\n"
				);
			help_client_commands();
			return (c == OPT_HELP ? 0 : 1);

		case OPT_VERSION:
			printf("%s %s\n", program_name, "0.1");
			return 0;

		case OPT_CONFIGFILE:
			ni_set_global_config_path(optarg);
			break;

		case OPT_DEBUG:
			if (!strcmp(optarg, "help")) {
				printf("Supported debug facilities:\n");
				ni_debug_help();
				return 0;
			}
			if (ni_enable_debug(optarg) < 0) {
				fprintf(stderr, "Bad debug facility \"%s\"\n", optarg);
				return 1;
			}
			if (!opt_log_level)
				ni_log_level_set("debug");
			break;

		case OPT_QUIET:
			opt_quiet = TRUE;
			break;

		case OPT_LOG_TARGET:
			opt_log_target = optarg;
			break;

		case OPT_LOG_LEVEL:
			opt_log_level = optarg;
			if (!ni_log_level_set(optarg)) {
				fprintf(stderr, "Bad log level \%s\"\n", optarg);
				return 1;
			}
			break;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing command\n");
		goto usage;
	}

	if (ni_init("client") < 0)
		return 1;

	if (opt_log_target == NULL) {
		ni_log_destination(program_name, "stderr");
	} else
	if (!ni_log_destination(program_name, opt_log_target)) {
		fprintf(stderr, "Bad log destination \%s\"\n", opt_log_target);
		return 1;
	}

	cmd = argv[optind];

	if (!strcmp(cmd, "help"))
		goto usage;

	if (!ni_objectmodel_register(&ni_testbus_objectmodel))
		ni_fatal("Cannot initialize objectmodel, giving up.");

	ni_testbus_client_init(NULL);

	handler = get_client_command(cmd);
	if (handler == NULL) {
		fprintf(stderr, "Unsupported command %s\n", cmd);
		goto usage;
	}

	return handler(argc - optind, argv + optind);
}

/* Hack */
struct ni_dbus_dict_entry {
	/* key of the dict entry */
	const char *            key;

	/* datum associated with key */
	ni_dbus_variant_t       datum;
};

static void	__dump_fake_xml(const ni_dbus_variant_t *, unsigned int, const char **);

static const char *
__fake_dbus_scalar_type(unsigned int type)
{
	static ni_intmap_t	__fake_dbus_types[] = {
		{ "byte",		DBUS_TYPE_BYTE		},
		{ "boolean",		DBUS_TYPE_BOOLEAN	},
		{ "int16",		DBUS_TYPE_INT16		},
		{ "uint16",		DBUS_TYPE_UINT16	},
		{ "int32",		DBUS_TYPE_INT32		},
		{ "uint32",		DBUS_TYPE_UINT32	},
		{ "int64",		DBUS_TYPE_INT64		},
		{ "uint64",		DBUS_TYPE_UINT64	},
		{ "double",		DBUS_TYPE_DOUBLE	},
		{ "string",		DBUS_TYPE_STRING	},
		{ "object-path",	DBUS_TYPE_OBJECT_PATH	},
		{ NULL },
	};

	return ni_format_uint_mapped(type, __fake_dbus_types);
}

static void
__dump_fake_xml_element(const ni_dbus_variant_t *var, unsigned int indent,
				const char *open_tag, const char *close_tag,
				const char **dict_elements)
{
	if (var->type == DBUS_TYPE_STRUCT) {
		unsigned int i;

		/* Must be a struct or union */
		printf("%*.*s<%s>\n", indent, indent, "", open_tag);
		for (i = 0; i < var->array.len; ++i) {
			ni_dbus_variant_t *member = &var->struct_value[i];
			char open_tag_buf[128], *member_open_tag;
			const char *basic_type;

			basic_type = __fake_dbus_scalar_type(member->type);
			if (basic_type == NULL) {
				member_open_tag = "member";
			} else {
				snprintf(open_tag_buf, sizeof(open_tag_buf), "member type=\"%s\"", basic_type);
				member_open_tag = open_tag_buf;
			}

			__dump_fake_xml_element(member, indent + 2, member_open_tag, "member", NULL);
		}
		printf("%*.*s</%s>\n", indent, indent, "", close_tag);
	} else
	if (var->type != DBUS_TYPE_ARRAY) {
		/* Must be some type of scalar */
		printf("%*.*s<%s>%s</%s>\n",
				indent, indent, "",
				open_tag,
				ni_dbus_variant_sprint(var),
				close_tag);
	} else if(var->array.len == 0) {
		printf("%*.*s<%s />\n", indent, indent, "", open_tag);
	} else if (ni_dbus_variant_is_byte_array(var)) {
		unsigned char value[64];
		unsigned int num_bytes;
		char display_buffer[128];
		const char *display;

		if (!ni_dbus_variant_get_byte_array_minmax(var, value, &num_bytes, 0, sizeof(value))) {
			display = "<INVALID />";
		} else {
			display = ni_format_hex(value, num_bytes, display_buffer, sizeof(display_buffer));
		}
		printf("%*.*s<%s>%s</%s>\n",
				indent, indent, "",
				open_tag,
				display,
				close_tag);
	} else {
		printf("%*.*s<%s>\n", indent, indent, "", open_tag);
		__dump_fake_xml(var, indent + 2, dict_elements);
		printf("%*.*s</%s>\n", indent, indent, "", close_tag);
	}
}

static void
__dump_fake_xml(const ni_dbus_variant_t *variant, unsigned int indent, const char **dict_elements)
{
	ni_dbus_dict_entry_t *entry;
	unsigned int index;

	if (ni_dbus_variant_is_dict(variant)) {
		const char *dict_element_tag = NULL;

		if (dict_elements && dict_elements[0])
			dict_element_tag = *dict_elements++;
		for (entry = variant->dict_array_value, index = 0; index < variant->array.len; ++index, ++entry) {
			const ni_dbus_variant_t *child = &entry->datum;
			const char *open_tag, *close_tag;
			char namebuf[256];

			if (dict_element_tag) {
				snprintf(namebuf, sizeof(namebuf), "%s name=\"%s\"", dict_element_tag, entry->key);
				open_tag = namebuf;
				close_tag = dict_element_tag;
			} else {
				open_tag = close_tag = entry->key;
			}

			__dump_fake_xml_element(child, indent, open_tag, close_tag, dict_elements);
		}
	} else if (ni_dbus_variant_is_dict_array(variant)) {
		const ni_dbus_variant_t *child;

		for (child = variant->variant_array_value, index = 0; index < variant->array.len; ++index, ++child) {
			printf("%*.*s<e>\n", indent, indent, "");
			__dump_fake_xml(child, indent + 2, NULL);
			printf("%*.*s</e>\n", indent, indent, "");
		}
	} else if (ni_dbus_variant_is_string_array(variant)) {
		unsigned int i;

		for (i = 0; i < variant->array.len; ++i) {
			printf("%*.*s<e>%s</e>\n", indent, indent, "",
					variant->string_array_value[i]);
		}
	} else {
		ni_error("%s: cannot handle signature \"%s\"", __func__, ni_dbus_variant_signature(variant));
	}
}

static xml_node_t *
__dump_object_xml(const char *object_path, const ni_dbus_variant_t *variant, ni_xs_scope_t *schema, xml_node_t *parent)
{
	xml_node_t *object_node;
	ni_dbus_dict_entry_t *entry;
	unsigned int index;

	if (!ni_dbus_variant_is_dict(variant)) {
		ni_error("%s: dbus data is not a dict", __func__);
		return NULL;
	}

	object_node = xml_node_new("object", parent);
	xml_node_add_attr(object_node, "path", object_path);

	for (entry = variant->dict_array_value, index = 0; index < variant->array.len; ++index, ++entry) {
		const char *interface_name = entry->key;

		/* Ignore well-known interfaces that never have properties */
		if (!strcmp(interface_name, "org.freedesktop.DBus.ObjectManager")
		 || !strcmp(interface_name, "org.freedesktop.DBus.Properties"))
			continue;

		ni_dbus_xml_deserialize_properties(schema, interface_name, &entry->datum, object_node);
	}

	return object_node;
}

static xml_node_t *
__dump_schema_xml(const ni_dbus_variant_t *variant, ni_xs_scope_t *schema)
{
	xml_node_t *root = xml_node_new(NULL, NULL);
	ni_dbus_dict_entry_t *entry;
	unsigned int index;

	if (!ni_dbus_variant_is_dict(variant)) {
		ni_error("%s: dbus data is not a dict", __func__);
		return NULL;
	}

	for (entry = variant->dict_array_value, index = 0; index < variant->array.len; ++index, ++entry) {
		if (!__dump_object_xml(entry->key, &entry->datum, schema, root))
			return NULL;
	}

	return root;
}


static int
do_show_xml(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_RAW, };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ "raw", no_argument, NULL, OPT_RAW },
		{ NULL }
	};
	ni_dbus_object_t *list_object;
	ni_dbus_variant_t result = NI_DBUS_VARIANT_INIT;
	DBusError error = DBUS_ERROR_INIT;
	int opt_raw = 0;
	int c, rv = 1;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		case OPT_RAW:
			opt_raw = 1;
			break;

		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"wicked [options] show-xml\n"
				"\nSupported options:\n"
				"  --help\n"
				"      Show this help text.\n"
				"  --raw\n"
				"      Show raw dbus reply in pseudo-xml, rather than using the schema\n"
				);
			return 1;
		}
	}

	if (optind != argc)
		goto usage;

	list_object = ni_testbus_client_get_object(NULL);
	if (!ni_dbus_object_call_variant(list_object,
			"org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
			0, NULL,
			1, &result, &error)) {
		ni_error("GetManagedObject call failed");
		goto out;
	}

	if (opt_raw) {
		static const char *dict_element_tags[] = {
			"object", "interface", NULL
		};

		__dump_fake_xml(&result, 0, dict_element_tags);
	} else {
		ni_xs_scope_t *schema = ni_objectmodel_get_schema();
		xml_node_t *tree;

		tree = __dump_schema_xml(&result, schema);
		if (tree == NULL) {
			ni_error("unable to represent properties as xml");
			goto out;
		}

		xml_node_print(tree, NULL);
		xml_node_free(tree);
	}

	rv = 0;

out:
	ni_dbus_variant_destroy(&result);
	return rv;
}

static int
do_create_host(int argc, char **argv)
{
	enum  { OPT_HELP, };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	const char *hostname;
	ni_dbus_object_t *host_object;
	int c, rv = 1;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"wicked [options] create-host name\n"
				"\nSupported options:\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;
		}
	}

	if (optind != argc - 1)
		goto usage;
	hostname = argv[optind++];

	host_object = ni_testbus_client_create_host(hostname);
	if (host_object == NULL)
		goto out;

	printf("%s\n", host_object->path);
	rv = 0;

out:
	return rv;
}

static int
do_remove_host(int argc, char **argv)
{
	enum  { OPT_HELP, };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	const char *hostname;
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"wicked [options] remove-host name\n"
				"\nSupported options:\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;
		}
	}

	if (optind != argc - 1)
		goto usage;
	hostname = argv[optind++];

	if (!ni_testbus_client_remove_host(hostname))
		return 1;

	return 0;
}

static int
do_delete_object(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_CONTEXT, OPT_NAME, OPT_CLASS };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ "name", no_argument, NULL, OPT_NAME },
		{ "class", required_argument, NULL, OPT_CLASS },
		{ "context", required_argument, NULL, OPT_CONTEXT },
		{ NULL }
	};
	ni_dbus_object_t *container_object = NULL;
	const char *opt_container = NULL;
	const char *opt_class = NULL;
	ni_bool_t opt_name = FALSE;
	int c, rv = 1;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"wicked [options] delete <object-handle> ...\n"
				"wicked [options] delete --name [--context <object-handle>] [--class <class>] nickname ...\n"
				"\nSupported options:\n"
				"  --name\n"
				"      Rather than specifying the object(s) by path, search by name within a given context object.\n"
				"      The search context is specified using the --context option. If not given, the global\n"
				"      context is searched.\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;

		case OPT_CLASS:
			opt_class = optarg;
			break;

		case OPT_CONTEXT:
			opt_container = optarg;
			break;

		case OPT_NAME:
			opt_name = TRUE;
			break;
		}
	}

	if (optind >= argc)
		goto usage;

	if (opt_name) {
		const ni_dbus_class_t *class = NULL;
		int failed = 0;

		if (opt_container == NULL)
			opt_container = NI_TESTBUS_GLOBAL_CONTEXT_PATH;

		container_object = ni_testbus_client_get_and_refresh_object(opt_container);
		if (container_object == NULL) {
			ni_error("Cannot look up context \"%s\"", opt_container);
			return 1;
		}

		if (opt_class) {
			class = ni_objectmodel_get_class(opt_class);
			if (class == NULL) {
				ni_error("Unknown object class \"%s\"", opt_class);
				return 1;
			}
		}

		while (optind < argc) {
			const char *name = argv[optind++];
			ni_dbus_object_t *object;
			int nfound = 0;

			do {
				object = ni_testbus_client_container_child_by_name(container_object, class, name);
				if (!object)
					break;
				nfound++;

				printf("Deleting %s\n", object->name);
				if (!ni_testbus_client_delete(object)) {
					ni_error("could not delete object %s", name);
					failed++;
				}
			} while (class == NULL);

			if (nfound == 0) {
				if (opt_class)
					ni_error("no %s object named \"%s\" in context %s", opt_class, name, opt_container);
				else
					ni_error("no object named \"%s\" in context %s", name, opt_container);
			}
		}

		rv = failed? 1 : 0;
	} else {
		int failed = 0;

		while (optind < argc) {
			const char *path = argv[optind++];
			ni_dbus_object_t *object;

			object = ni_testbus_client_get_and_refresh_object(path);
			if (object == NULL) {
				ni_error("no such object %s", path);
				failed++;
				continue;
			}

			ni_trace("delete %s", object->path);
			if (!ni_testbus_client_delete(object)) {
				ni_error("could not delete object %s", path);
				failed++;
				continue;
			}
		}

		rv = failed? 1 : 0;
	}

	return rv;
}

static int
do_create_test(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_CONTEXT };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ "context", required_argument, NULL, OPT_CONTEXT },
		{ NULL }
	};
	ni_dbus_object_t *container_object = NULL, *test_object;
	const char *opt_container = NULL;
	const char *testname;
	int c, rv = 1;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"wicked [options] create-test name\n"
				"\nSupported options:\n"
				"  --container <path>\n"
				"      Specify the container in which the test is to be created\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;

		case OPT_CONTEXT:
			opt_container = optarg;
			break;
		}
	}

	if (optind != argc - 1)
		goto usage;
	testname = argv[optind++];

	if (opt_container
	 && !(container_object = ni_testbus_client_get_container(opt_container)))
		return 1;

	test_object = ni_testbus_client_create_test(testname, container_object);
	if (test_object == NULL)
		goto out;

	printf("%s\n", test_object->path);
	rv = 0;

out:
	return rv;
}

static int
do_setenv(int argc, char **argv)
{
	enum  { OPT_HELP, };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	const char *opt_context;
	ni_dbus_object_t *context_object;
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] setenv object-path name=value ...\n"
				"\nSupported options:\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;
		}
	}

	if (optind >= argc - 1)
		goto usage;
	opt_context = argv[optind++];

	context_object = ni_testbus_client_get_container(opt_context);
	if (context_object == NULL)
		return 1;

	while (optind < argc) {
		char *name, *value;

		name = argv[optind++];
		if (!(value = strchr(name, '=')))
			ni_fatal("setenv: argument must be name=value");
		*value++ = '\0';

		ni_testbus_client_setenv(context_object, name, value);
	}

	return 0;
}

static int
do_getenv(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_EXPORT };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ "export", no_argument, NULL, OPT_EXPORT },
		{ NULL }
	};
	const char *opt_context;
	ni_dbus_object_t *context_object;
	ni_bool_t opt_export = FALSE;
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] getenv object-path name=value ...\n"
				"\nSupported options:\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;

		case OPT_EXPORT:
			opt_export = TRUE;
			break;
		}
	}

	if (optind >= argc - 1)
		goto usage;
	opt_context = argv[optind++];

	context_object = ni_testbus_client_get_container(opt_context);
	if (context_object == NULL)
		return 1;

	while (optind < argc) {
		char *name, *value;

		name = argv[optind++];
		value = ni_testbus_client_getenv(context_object, name);
		if (value == NULL) {
			ni_error("getenv failed");
			return 1;
		}

		if (opt_export) {
			char *quoted = ni_quote(value, NULL);

			printf("%s=%s\n", name, quoted);
			free(quoted);
		} else {
			printf("%s\n", value);
		}
		free(value);
	}

	return 0;
}

static int
__do_shutdown_reboot(unsigned int nhosts, char **host_names, ni_bool_t opt_reboot, ni_bool_t opt_wait, long opt_timeout)
{
	ni_testus_client_host_state_t *host_list = NULL;
	unsigned int i;

	if (nhosts == 1 && ni_string_eq(host_names[0], "all")) {
		ni_dbus_object_t *hostlist;

		if (opt_wait) {
			ni_error("shutdown of \"all\" hosts: option --wait currently not supported");
			return 1;
		}

		hostlist = ni_testbus_client_get_and_refresh_object(NI_TESTBUS_HOSTLIST_PATH);
		if (!ni_testbus_client_host_shutdown(hostlist, opt_reboot, NULL))
			return 1;

		return 0;
	}

	host_list = ni_calloc(nhosts, sizeof(host_list[0]));
	for (i = 0; i < nhosts; ++i) {
		const char *path = host_names[i];
		ni_dbus_object_t *object;

		object = ni_testbus_client_get_and_refresh_object(path);
		if (object == NULL) {
			ni_error("unknown host object %s", path);
			return 1;
		}
		host_list[i].host_object = object;
	}

	for (i = 0; i < nhosts; ++i) {
		ni_testus_client_host_state_t *state = &host_list[i];
		ni_dbus_object_t *object = state->host_object;

		if (!ni_testbus_client_host_shutdown(object, opt_reboot, state)) {
			ni_error("Unable to reboot host %s", object->path);
			continue;
		}
		if (opt_wait && state->host_gen == 0)
			ni_warn("Unable to wait for host \"%s\" to come back - no generation number", object->path);
	}

	if (opt_wait) {
		ni_testbus_client_timeout_t timeout, *to = NULL;

		if (opt_timeout >= 0) {
			ni_testbus_client_timeout_init(&timeout, opt_timeout);

			if (!ni_debug && !opt_quiet) {
				timeout.busy_wait = __do_claim_host_busywait;
				timeout.timedout = __do_claim_host_timedout;
			}
			to = &timeout;
		}

		if (!ni_testbus_client_host_wait_for_reboot(nhosts, host_list, to)) {
			for (i = 0; i < nhosts; ++i) {
				if (!host_list[i].ready)
					ni_error("Timed out waiting for %s", host_list[i].host_object->path);
			}
		} else {
			if (to && to->num_busywaits)
				fprintf(stderr, "\n");
		}
	}

	return 0;
}

static int
do_shutdown(int argc, char **argv)
{
	enum  { OPT_HELP, };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] shutdown object-path ...\n"
				"testbus [options] shutdown all\n"
				"\nSupported options:\n"
				"  --help\n"
				"      Show this help text.\n"
				"The object paths can refer both to individual hosts or containers\n"
				"that hold hosts. Specifying \"all\" will shut down all hosts currently\n"
				"registered with the testbus master\n"
				);
			return 1;

		}
	}

	if (optind >= argc)
		goto usage;

	return __do_shutdown_reboot(argc - optind, argv + optind, FALSE, FALSE, -1);
}

static int
do_reboot(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_WAIT, OPT_TIMEOUT, };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ "wait", no_argument, NULL, OPT_WAIT },
		{ "timeout", required_argument, NULL, OPT_TIMEOUT },
		{ NULL }
	};
	ni_bool_t opt_wait = FALSE;
	long opt_timeout = -1;
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] reboot object-path ...\n"
				"testbus [options] reboot all\n"
				"\nSupported options:\n"
				"  --wait\n"
				"      Wait for host to come back (with reboot only)\n"
				"  --timeout <msec>\n"
				"      When waiting for a host to reboot, time out after <msec> milliseconds\n"
				"  --help\n"
				"      Show this help text.\n"
				"The object paths can refer both to individual hosts or containers\n"
				"that hold hosts. Specifying \"all\" will shut down all hosts currently\n"
				"registered with the testbus master\n"
				);
			return 1;

		case OPT_WAIT:
			opt_wait = TRUE;
			break;

		case OPT_TIMEOUT:
			if (ni_parse_long(optarg, &opt_timeout, 10) < 0) {
				ni_error("could not parse timeout value");
				return 1;
			}
			if (opt_timeout < 0) {
				ni_warn("ignoring negative timeout value");
				opt_timeout = -1;
			} else {
				opt_timeout *= 1000;
			}
			break;

		}
	}

	if (optind >= argc)
		goto usage;

	if (opt_timeout && !opt_wait) {
		ni_error("Cannot use --timeout without --wait");
		goto usage;
	}

	return __do_shutdown_reboot(argc - optind, argv + optind, TRUE, opt_wait, opt_timeout);
}

static int
do_download_file(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_HOST, OPT_CONTEXT };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ "host", required_argument, NULL, OPT_HOST },
		{ "context", required_argument, NULL, OPT_CONTEXT },
		{ NULL }
	};
	const char *opt_hostname = NULL;
	const char *opt_context = NULL;
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] download-file remote-path local-path\n"
				"\nSupported options:\n"
				"  --host <hostname>\n"
				"      Specify the host to download the file from. A testbus agent\n"
				"      must be running on the remote host.\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;

		case OPT_HOST:
			opt_hostname = optarg;
			break;

		case OPT_CONTEXT:
			opt_context = optarg;
			break;
		}
	}

	if (!((!opt_hostname) ^ (!opt_context))) {
		ni_error("You must specify exactly one --host or --context option");
		goto usage;
	}

	if (opt_hostname) {
		ni_dbus_object_t *agent_object;
		const char *remote_path, *local_path;
		ni_buffer_t *data;
		int written;

		if (optind > argc - 2)
			goto usage;
		remote_path = argv[optind++];
		local_path = argv[optind++];

		agent_object = ni_testbus_client_get_agent(opt_hostname);
		if (agent_object == NULL)
			return 1;

		ni_debug_testbus("created agent handle");
		data = ni_testbus_client_agent_download_file(agent_object, remote_path);
		if (data == NULL) {
			ni_error("Unable to download \"%s\" from %s", remote_path, opt_hostname);
			return 1;
		}

		written = ni_testbus_write_local_file(local_path, data);
		ni_buffer_free(data);

		if (written < 0) {
			ni_error("error writing \"%s\"", local_path);
			return 1;
		}
	} else {
		ni_error("download from container object not yet implemented");
		return 1;
	}

	return 0;
}

static int
do_upload_file(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_HOST, OPT_CONTEXT };
	static struct option local_options[] = {
		{ "host", required_argument, NULL, OPT_HOST },
		{ "context", required_argument, NULL, OPT_CONTEXT },
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	const char *opt_hostname = NULL;
	const char *opt_context = NULL;
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] upload-file --host <host-object> path <destpath>\n"
				"testbus [options] upload-file --context <container-object> path <identifier>\n"
				"\nSupported options:\n"
				"  --host <host-object>\n"
				"      Argument is a DBus object path. The file is uploaded directly to the host's filesystem.\n"
				"  --context <container-object>\n"
				"      Argument is a DBus object path. The file is uploaded to the testbus master, and is distributed\n"
				"      from there to all hosts the test runs on.\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;

		case OPT_HOST:
			opt_hostname = optarg;
			break;

		case OPT_CONTEXT:
			opt_context = optarg;
			break;
		}
	}

	if (opt_hostname && opt_context) {
		ni_error("--host and --context options are mutually exclusive");
		goto usage;
	}
	if (!opt_hostname && !opt_context) {
		ni_error("No --host and --context option specified");
		goto usage;
	}

	if (opt_hostname) {
		const char *local_path, *remote_path;
		ni_dbus_object_t *agent_object;
		ni_buffer_t *data;
		ni_bool_t rv;

		if (optind != argc - 2)
			goto usage;
		local_path = argv[optind++];
		remote_path = argv[optind++];

		agent_object = ni_testbus_client_get_agent(opt_hostname);
		if (agent_object == NULL)
			return 1;

		data = ni_testbus_read_local_file(local_path);
		if (!data)
			return 1;

		rv = ni_testbus_client_agent_upload_file(agent_object, remote_path, data);
		ni_buffer_free(data);

		if (!rv) {
			ni_error("error uploading \"%s\" to \"%s\" on %s",
					local_path, remote_path, opt_hostname);
			return 1;
		}
	} else {
		const char *local_path, *identifier;
		ni_dbus_object_t *context_object, *file_object;
		ni_buffer_t *data;
		unsigned int count;

		if (optind != argc - 2)
			goto usage;
		local_path = argv[optind++];
		identifier = argv[optind++];

		context_object = ni_testbus_client_get_container(opt_context);
		if (context_object == NULL)
			return 1;

		data = ni_testbus_read_local_file(local_path);
		if (!data)
			return 1;
		count = ni_buffer_count(data);

		file_object = ni_testbus_client_create_tempfile(identifier, NI_TESTBUS_FILE_READ, context_object);
		if (file_object == NULL)
			return 1;

		if (!ni_testbus_client_upload_file(file_object, data))
			return 1;

		printf("Uploaded %u bytes\n", count);
		ni_buffer_free(data);
	}

	return 0;
}

static int
__do_claim_host_busywait(const ni_testbus_client_timeout_t *client_timeout)
{
	if (client_timeout->num_busywaits == 0)
		fprintf(stderr, "Waiting for host to become ready");
	fputc('.', stderr);
	fflush(stderr);

	return 1000; /* come back in 1 second */
}

static void
__do_claim_host_timedout(const ni_testbus_client_timeout_t *client_timeout)
{
	fprintf(stderr, " timed out.\n");
}

static int
do_claim_host(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_HOSTNAME, OPT_CAPABILITY, OPT_ROLE, OPT_TIMEOUT };
	static struct option local_options[] = {
		{ "hostname", required_argument, NULL, OPT_HOSTNAME },
		{ "capability", required_argument, NULL, OPT_CAPABILITY },
		{ "role", required_argument, NULL, OPT_ROLE },
		{ "timeout", required_argument, NULL, OPT_TIMEOUT },
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	ni_dbus_object_t *container_object, *host_object;
	const char *opt_hostname = NULL;
	const char *opt_capability = NULL;
	const char *opt_role = NULL;
	unsigned int opt_timeout = 0;
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
			ni_error("Unsupported option %d", optind);
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] claim-host --hostname <name> [--role <role>] <container-path>\n"
				"testbus [options] claim-host --capability <name> [--role <role>] <container-path>\n"
				"\nSupported options:\n"
				"  --hostname <name>\n"
				"      Argument is a hostname, as registered by the agent.\n"
				"  --capability <name>\n"
				"      Argument is a capability string; this will only claim hosts having registered\n"
				"      this capability.\n"
				"      The capability \"any\" will match any host. This is the default if neither this\n"
				"      option nor --hostname is given.\n"
				"  --role <role>\n"
				"      This will add the host using the specified role. If no role is given,\n"
				"      it will default to \"testhost\".\n"
				"  --timeout <seconds>\n"
				"      If there is no unsed host matching the requested capabilities,\n"
				"      wait for it to come online.\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;

		case OPT_HOSTNAME:
			opt_hostname = optarg;
			break;

		case OPT_CAPABILITY:
			opt_capability = optarg;
			break;

		case OPT_ROLE:
			opt_role = optarg;
			break;

		case OPT_TIMEOUT:
			if (ni_parse_uint(optarg, &opt_timeout, 0) < 0) {
				ni_error("cannot parse timeout argument");
				goto usage;
			}
			break;
		}
	}

	if (!opt_role)
		opt_role = "testhost";

	if (opt_hostname && opt_capability) {
		ni_error("you cannot use --hostname and --capability at the same time");
		return 1;
	}

	if (optind != argc - 1) {
		ni_error("Bad number of arguments");
		goto usage;
	} else {
		const char *container_path = argv[optind++];

		container_object = ni_testbus_client_get_and_refresh_object(container_path);
		if (container_object == NULL) {
			ni_error("unknown container object %s", container_path);
			return 1;
		}
	}

	if (opt_hostname) {
		host_object = ni_testbus_client_claim_host_by_name(opt_hostname, container_object, opt_role);
	} else {
		ni_testbus_client_timeout_t timeout, *tmo = NULL;

		if (opt_capability == NULL)
			opt_capability = "any";

		if (opt_timeout) {
			ni_testbus_client_timeout_init(&timeout, opt_timeout * 1000);

			if (!ni_debug && !opt_quiet) {
				timeout.busy_wait = __do_claim_host_busywait;
				timeout.timedout = __do_claim_host_timedout;
			}
			tmo = &timeout;
		}

		host_object = ni_testbus_client_claim_host_by_capability(opt_capability, container_object, opt_role, tmo);

		if (tmo && tmo->num_busywaits)
			fprintf(stderr, "\n");
	}

	if (host_object == NULL) {
		ni_error("Unable to claim host.");
		return 1;
	}

	printf("%s\n", host_object->path);
	return 0;
}

/*
 * Check if two fds refer to the same file
 */
static ni_bool_t
__samefile(int fd1, int fd2)
{
	struct stat stb1, stb2;

	if (fstat(fd1, &stb1) < 0) {
		ni_error("samefile(%d): %m", fd1);
		return FALSE;
	}
	if (fstat(fd2, &stb2) < 0) {
		ni_error("samefile(%d): %m", fd1);
		return FALSE;
	}

	return stb1.st_dev == stb2.st_dev
	    && stb1.st_ino == stb2.st_ino;
}


/*
 * Helper function for creating a command
 */
static ni_dbus_object_t *
__do_create_command(ni_dbus_object_t *container_object, int argc, char **argv, ni_bool_t send_stdin, ni_bool_t send_script, ni_bool_t use_terminal)
{
	ni_string_array_t command_argv = NI_STRING_ARRAY_INIT;
	ni_dbus_object_t *cmd_object;
	const char *script_file = NULL;
	int index;

	if (send_script) {
		script_file = argv[0];
		argv[0] = "%{file:script}";
	}

	for (index = 0; index < argc; ++index)
		ni_string_array_append(&command_argv, argv[index]);

	cmd_object = ni_testbus_client_create_command(container_object, &command_argv, use_terminal);
	ni_string_array_destroy(&command_argv);

	if (send_stdin) {
		ni_buffer_t *data;

		data = ni_file_read(stdin);
		ni_testbus_client_command_add_file(cmd_object, "stdin", data, NI_TESTBUS_FILE_READ);
		ni_buffer_free(data);
	}

	if (script_file) {
		ni_buffer_t *data;

		data = ni_testbus_read_local_file(script_file);
		if (data == NULL) {
			ni_error("cannot send script %s: %m", script_file);
			return NULL;
		}

		ni_testbus_client_command_add_file(cmd_object, "script", data, NI_TESTBUS_FILE_READ | NI_TESTBUS_FILE_EXEC);
		ni_buffer_free(data);
	}

	/* FIXME: when capturing stdout/stderr, we should check whether we our own
	 * stdout/err descriptors refer to the same file or different ones.
	 *
	 * If they refer to the same file, we want the server to merge the output and
	 * send it to us as one - this preserves the order of the output as written
	 * out by the command on the server side.
	 *
	 * If they refer to different files, we should tell the server to capture
	 * these separately.
	 */
	ni_testbus_client_create_tempfile("stdout", NI_TESTBUS_FILE_WRITE, cmd_object);
	if (!__samefile(1, 2)) {
		ni_testbus_client_create_tempfile("stderr", NI_TESTBUS_FILE_WRITE, cmd_object);
	}

	return cmd_object;
}

/*
 * Flush an output file of the process to stdout/stderr
 */
static void
flush_process_file(ni_dbus_object_t *proc_object, const char *filename, FILE *ofp, ni_bool_t safe_output)
{
	ni_dbus_object_t *file_object;
	ni_buffer_t *data;

	ni_debug_testbus("%s(%s, %s)", __func__, proc_object->path, filename);
	file_object = ni_testbus_client_container_child_by_name(proc_object,
					ni_testbus_file_class(),
					filename);
	if (file_object == NULL) {
		ni_debug_testbus("%s: no file named %s", proc_object->path, filename);
		return;
	}

	/* Now download the file */
	data = ni_testbus_client_download_file(file_object);
	if (data == NULL) {
		ni_error("failed to download %s", filename);
		return;
	}

	ni_debug_testbus("downloaded file %s (%s)", filename, file_object->path);

	if (safe_output)
		ni_file_write_safe(ofp, data);
	else
		ni_file_write(ofp, data);
	ni_buffer_free(data);
}

/*
 * Create a command on a given host.
 * This can be executed via run-command, or scheduled for async execution.
 */
static int
do_create_command(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_CONTEXT, };
	static struct option local_options[] = {
		{ "context", required_argument, NULL, OPT_CONTEXT },
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	ni_dbus_object_t *container_object, *cmd_object;
	const char *opt_container = NULL;
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] create-command --context <object-path> command args...\n"
				"\nSupported options:\n"
				"  --context <object-path>\n"
				"      Argument is a the object path of a container object, such as a testcase or a test group\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;

		case OPT_CONTEXT:
			opt_container = optarg;
			break;

		}
	}

	if (opt_container != 0) {
		container_object = ni_testbus_client_get_and_refresh_object(opt_container);
		if (container_object == NULL) {
			ni_error("unknown host object %s", opt_container);
			return 1;
		}
	} else {
		ni_error("You have to specify a context when creating a command");
		return 1;
	}

	if (optind > argc - 1)
		goto usage;

	cmd_object = __do_create_command(container_object, argc - optind, argv + optind, FALSE, FALSE, FALSE);
	if (cmd_object == NULL)
		return 1;

	printf("%s\n", cmd_object->path);
	return 0;
}

/*
 * Common code for waiting for a command and displaying its output
 */
static int
__do_wait_command(ni_dbus_object_t *proc_object, unsigned int timeout_ms, ni_bool_t safe_output)
{
	ni_process_exit_info_t exit_info;

	if (!ni_testbus_wait_for_process(proc_object, timeout_ms, &exit_info)) {
		ni_error("failed to wait for process to complete");
		return 1;
	}

	if (exit_info.stdout_bytes)
		flush_process_file(proc_object, "stdout", stdout, safe_output);
	if (exit_info.stderr_bytes)
		flush_process_file(proc_object, "stderr", stderr, safe_output);

	ni_testbus_client_delete(proc_object);

	switch (exit_info.how) {
	case NI_PROCESS_NONSTARTER:
		ni_error("failed to start process");
		return 1;

	case NI_PROCESS_CRASHED:
		ni_error("process crashed with signal %u%s",
				exit_info.crash.signal,
				exit_info.crash.core_dumped? " (core dumped)" : "");
		return 1;

	case NI_PROCESS_EXITED:
		return exit_info.exit.code;

	case NI_PROCESS_TIMED_OUT:
		ni_error("timed out waiting for process to complete");
		return 1;

	default:
		ni_error("process disappeared into Nirvana");
		return 1;
	}

	return 0;
}

/*
 * Run a command on a given host, block and wait for the result
 */
static int
do_run_command(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_HOSTPATH, OPT_CONTEXT, OPT_SEND_STDIN, OPT_SEND_SCRIPT, OPT_USE_TERMINAL, OPT_NO_OUPUT_PROCESSING, OPT_TIMEOUT, OPT_NO_WAIT };
	static struct option local_options[] = {
		{ "host", required_argument, NULL, OPT_HOSTPATH },
		{ "context", required_argument, NULL, OPT_CONTEXT },
		{ "send-stdin", no_argument, NULL, OPT_SEND_STDIN },
		{ "send-script", no_argument, NULL, OPT_SEND_SCRIPT },
		{ "use-terminal", no_argument, NULL, OPT_USE_TERMINAL },
		{ "no-output-processing", no_argument, NULL, OPT_NO_OUPUT_PROCESSING },
		{ "timeout", required_argument, NULL, OPT_TIMEOUT },
		{ "nowait", no_argument, NULL, OPT_NO_WAIT },
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	ni_dbus_object_t *ctxt_object, *host_object, *cmd_object, *proc_object;
	const char *opt_hostpath = NULL;
	const char *opt_contextpath = NULL;
	ni_bool_t opt_send_stdin = FALSE;
	ni_bool_t opt_send_script = FALSE;
	ni_bool_t opt_use_terminal = FALSE;
	ni_bool_t opt_safe_output = TRUE;
	ni_bool_t opt_wait_for_process = TRUE;
	long opt_timeout = -1;
	int c, rv;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] run-command --host <object-path> [--send-stdin] command args...\n"
				"\nSupported options:\n"
				"  --host <object-path>\n"
				"      Argument is a host object path, as returned by claim-host.\n"
				"  --send-stdin\n"
				"      Send the stdin of this command to the executing host, and pipe it into the command\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;

		case OPT_HOSTPATH:
			opt_hostpath = optarg;
			break;

		case OPT_CONTEXT:
			opt_contextpath = optarg;
			break;

		case OPT_SEND_STDIN:
			opt_send_stdin = TRUE;
			break;

		case OPT_SEND_SCRIPT:
			opt_send_script = TRUE;
			break;

		case OPT_USE_TERMINAL:
			opt_use_terminal = TRUE;
			break;

		case OPT_NO_OUPUT_PROCESSING:
			opt_safe_output = FALSE;
			break;

		case OPT_TIMEOUT:
			if (ni_parse_long(optarg, &opt_timeout, 10) < 0) {
				ni_error("could not parse timeout value");
				return 1;
			}
			if (opt_timeout < 0) {
				ni_warn("ignoring negative timeout value");
				opt_timeout = -1;
			} else {
				opt_timeout *= 1000;
			}
			break;

		case OPT_NO_WAIT:
			opt_wait_for_process = FALSE;
			break;
		}
	}

	if (opt_hostpath != 0) {
		host_object = ni_testbus_client_get_and_refresh_object(opt_hostpath);
		if (host_object == NULL) {
			ni_error("unknown host object %s", opt_hostpath);
			return 1;
		}
	} else {
		ni_error("Don't know which host to run this on");
		return 1;
	}

	if (opt_contextpath != 0) {
		ctxt_object = ni_testbus_client_get_and_refresh_object(opt_contextpath);
		if (ctxt_object == NULL) {
			ni_error("unknown context object %s", opt_contextpath);
			return 1;
		}
	} else {
		ctxt_object = host_object;
	}

	if (optind > argc - 1)
		goto usage;

	cmd_object = __do_create_command(ctxt_object, argc - optind, argv + optind, opt_send_stdin, opt_send_script, opt_use_terminal);
	if (!cmd_object)
		return 1;

	proc_object = ni_testbus_client_host_run(host_object, cmd_object);
	if (!proc_object)
		return 1;

	if (!opt_wait_for_process) {
		/* Don't wait for the process to complete, just print the object handle */
		printf("%s\n", proc_object->path);
		return 0;
	}

	rv = __do_wait_command(proc_object, opt_timeout, opt_safe_output);
	ni_testbus_client_delete(cmd_object);

	return rv;
}

/*
 * Wait for a given command to complete
 */
static int
do_wait_command(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_TIMEOUT };
	static struct option local_options[] = {
		{ "timeout", required_argument, NULL, OPT_TIMEOUT },
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	ni_dbus_object_t *proc_object;
	ni_bool_t opt_safe_output = TRUE;
	long opt_timeout = -1;
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] wait-command [options] <command-path>\n"
				"\nSupported options:\n"
				"  --timeout <count>\n"
				"      If the command is not done yet, wait for up to <count>\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;

		case OPT_TIMEOUT:
			if (ni_parse_long(optarg, &opt_timeout, 10) < 0) {
				ni_error("could not parse timeout value");
				return 1;
			}
			if (opt_timeout < 0) {
				ni_warn("ignoring negative timeout value");
				opt_timeout = -1;
			} else {
				opt_timeout *= 1000;
			}
			break;
		}
	}

	if (optind != argc - 1)
		goto usage;

	proc_object = ni_testbus_client_get_object(argv[optind]);
	if (!proc_object)
		return 1;

	return __do_wait_command(proc_object, opt_timeout, opt_safe_output);
}

/*
 * Retrieve eventlog
 */
static void
show_events(const ni_dbus_object_t *host_object, unsigned int *seq_seen, int output_mode)
{
	const ni_dbus_variant_t *var;
	unsigned int i;

	var = ni_dbus_object_get_cached_property(host_object, "events", ni_testbus_eventlog_interface());
	if (var == NULL) {
		printf("%s: no event log\n", host_object->path);
		return;
	}

	if (seq_seen)
		*seq_seen = 0;

	for (i = 0; TRUE; ++i) {
		ni_event_t event = NI_EVENT_INIT;
		const ni_dbus_variant_t *evdict;

		if (!(evdict = ni_dbus_dict_array_at(var, i)))
			break;
		if (!ni_testbus_event_deserialize(evdict, &event)) {
			ni_error("%s: bad event at index %u", host_object->path, i);
			break;
		}

		printf("%3u %lu.%06lu %-12s %-12s %-12s",
				event.sequence,
				event.timestamp.tv_sec,
				event.timestamp.tv_usec,
				event.class,
				event.source,
				event.type);

		if (event.data == NULL) {
			printf(" (no data)\n");
		} else {
			ni_bool_t first_line = TRUE;
			unsigned int len;

			while ((len = ni_buffer_count(event.data)) != 0) {
				char *head, *eol;

				head = ni_buffer_head(event.data);
				eol = memchr(head, '\n', len);
				if (eol) {
					len = eol - head;
					ni_buffer_pull_head(event.data, len + 1);
				} else {
					ni_buffer_pull_head(event.data, len);
				}
				if (len) {
					if (!first_line)
						printf("%*.*s", 60, 60, "");

					printf("%s\n", ni_print_suspect(head, len, output_mode));
					first_line = FALSE;
				}
			}
		}

		if (seq_seen && *seq_seen < event.sequence)
			*seq_seen = event.sequence;

		ni_event_destroy(&event);
	}
}

static int
do_get_events(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_PURGE, OPT_SAFE_OUTPUT };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ "purge", no_argument, NULL, OPT_PURGE },
		{ "safe-output", no_argument, NULL, OPT_SAFE_OUTPUT },
		{ NULL }
	};
	ni_dbus_object_t *host_objects[argc];
	ni_bool_t opt_purge = FALSE;
	int opt_output_mode = NI_PRINTABLE_NOCONTROL;
	int c, i, nhosts = 0;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] get-events object-path ...\n"
				"\nSupported options:\n"
				"  --purge\n"
				"      Purge event log after reading it.\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;

		case OPT_PURGE:
			opt_purge = TRUE;
			break;

		case OPT_SAFE_OUTPUT:
			opt_output_mode = NI_PRINTABLE_SHELL;
			break;
		}
	}

	if (optind >= argc)
		goto usage;

	if (optind + 1 == argc && ni_string_eq(argv[optind], "all")) {
		ni_dbus_object_t *hostlist, *object;

		hostlist = ni_testbus_client_get_and_refresh_object(NI_TESTBUS_HOSTLIST_PATH);
		if (hostlist == NULL) {
			ni_error("unable to refresh host list");
			return 1;
		}

		for (object = hostlist->children; object; object = object->next) {
			unsigned int seq_seen;

			show_events(object, &seq_seen, opt_output_mode);
			if (opt_purge && seq_seen)
				ni_testbus_client_eventlog_purge(object, seq_seen);
		}

		return 0;
	}

	while (optind < argc) {
		const char *path = argv[optind++];
		ni_dbus_object_t *object;

		object = ni_testbus_client_get_and_refresh_object(path);
		if (object == NULL) {
			ni_error("unknown host object %s", path);
			return 1;
		}
		host_objects[nhosts++] = object;
	}

	for (i = 0; i < nhosts; ++i) {
		unsigned int seq_seen;

		show_events(host_objects[i], &seq_seen, opt_output_mode);
		if (opt_purge && seq_seen)
			ni_testbus_client_eventlog_purge(host_objects[i], seq_seen);
	}

	return 0;
}

long
ni_testbus_write_local_file(const char *filename, const ni_buffer_t *data)
{
	if (!ni_string_eq(filename, "-"))
		return ni_file_write_path(filename, data);
	return ni_file_write(stdout, data);
}

ni_buffer_t *
ni_testbus_read_local_file(const char *filename)
{
	ni_buffer_t *result;
	FILE *fp;

	if (ni_string_eq(filename, "-")) {
		result = ni_file_read(stdin);
	} else {
		if (!(fp = fopen(filename, "r"))) {
			ni_error("unable to open %s: %m", filename);
			return NULL;
		}

		result = ni_file_read(fp);
		fclose(fp);
	}

	return result;
}

/*
 * Command table
 */
static struct client_command	client_command_table[] = {
	{ "show-xml",		do_show_xml,		"Show master state as XML"			},
	{ "delete",		do_delete_object,	"Delete an object"				},
	{ "create-host",	do_create_host,		"Create a new host object"			},
	{ "remove-host",	do_remove_host,		"Remove a host from a container"		},
	{ "create-test",	do_create_test,		"Create a test containt"			},
	{ "download-file",	do_download_file,	"Download output file from container"		},
	{ "upload-file",	do_upload_file,		"Upload input file to container"		},
	{ "claim-host",		do_claim_host,		"Claim a host for a test/container"		},
	{ "create-command",	do_create_command,	"Create command container"			},
	{ "run-command",	do_run_command,		"Run command/script on one or more hosts"	},
	{ "wait-command",	do_wait_command,	"Wait for backgrounded command to complete"	},
	{ "setenv",		do_setenv,		"Set environment variable in container"		},
	{ "getenv",		do_getenv,		"Get container variable"			},
	{ "get-events",		do_get_events,		"Get the event log"				},
	{ "shutdown",		do_shutdown,		"Shutdown agent"				},
	{ "reboot",		do_reboot,		"Reboot agent"					},

	{ NULL, }
};

static client_command_handler_t *
get_client_command(const char *name)
{
	struct client_command *cmd;

	for (cmd = client_command_table; cmd->name; ++cmd) {
		if (ni_string_eq(cmd->name, name))
			return cmd->handler;
	}

	return NULL;
}

static void
help_client_commands(void)
{
	struct client_command *cmd;

	for (cmd = client_command_table; cmd->name; ++cmd) {
		printf("  %s\n"
		       "        %s\n",
		       cmd->name,
		       cmd->description);
	}
}
