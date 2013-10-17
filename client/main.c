/*
 * Copyright (C) 2010-2013 Olaf Kirch <okir@suse.de>
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

enum {
	OPT_HELP,
	OPT_VERSION,
	OPT_CONFIGFILE,
	OPT_DEBUG,
	OPT_LOG_LEVEL,
	OPT_LOG_TARGET,

	OPT_DRYRUN,
	OPT_ROOTDIR,
};

static struct option	options[] = {
	/* common */
	{ "help",		no_argument,		NULL,	OPT_HELP },
	{ "version",		no_argument,		NULL,	OPT_VERSION },
	{ "config",		required_argument,	NULL,	OPT_CONFIGFILE },
	{ "debug",		required_argument,	NULL,	OPT_DEBUG },
	{ "log-level",		required_argument,	NULL,	OPT_LOG_LEVEL },
	{ "log-target",		required_argument,	NULL,	OPT_LOG_TARGET },

	/* specific */
	{ "dryrun",		no_argument,		NULL,	OPT_DRYRUN },
	{ "dry-run",		no_argument,		NULL,	OPT_DRYRUN },
	{ "root-directory",	required_argument,	NULL,	OPT_ROOTDIR },

	{ NULL }
};

static const char *	program_name;
static const char *	opt_log_level;
static const char *	opt_log_target;
int			opt_global_dryrun;
char *			opt_global_rootdir;

static int		do_show_xml(int, char **);
static int		do_delete_object(int, char **);
static int		do_create_host(int, char **);
static int		do_remove_host(int, char **);
static int		do_create_test(int, char **);
static int		do_retrieve_file(int, char **);
static int		do_upload_file(int, char **);
static int		do_claim_host(int, char **);
static int		do_create_command(int, char **);
static int		do_run_command(int, char **);

static ni_buffer_t *	ni_testbus_read_local_file(const char *);

int
main(int argc, char **argv)
{
	char *cmd;
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
				"  --dry-run\n"
				"        Do not change the system in any way.\n"
				"  --root-directory\n"
				"        Search all config files below this directory.\n"
				"\n"
				"Supported commands:\n"
				"  ...\n"
				);
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

		case OPT_DRYRUN:
			opt_global_dryrun = 1;
			break;

		case OPT_ROOTDIR:
			opt_global_rootdir = optarg;
			break;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing command\n");
		goto usage;
	}

	if (opt_log_target) {
		if (!ni_log_destination(program_name, opt_log_target)) {
			fprintf(stderr, "Bad log destination \%s\"\n",
				opt_log_target);
			return 1;
		}
	} else {
		ni_log_destination(program_name, "perror:user");
	}

	if (ni_init("client") < 0)
		return 1;

	cmd = argv[optind];

	if (!strcmp(cmd, "help"))
		goto usage;

	if (!ni_objectmodel_register(&ni_testbus_objectmodel))
		ni_fatal("Cannot initialize objectmodel, giving up.");

	ni_call_init_client(NULL);

	if (!strcmp(cmd, "show-xml"))
		return do_show_xml(argc - optind, argv + optind);
	if (!strcmp(cmd, "delete"))
		return do_delete_object(argc - optind, argv + optind);
	if (!strcmp(cmd, "create-host"))
		return do_create_host(argc - optind, argv + optind);
	if (!strcmp(cmd, "remove-host"))
		return do_remove_host(argc - optind, argv + optind);
	if (!strcmp(cmd, "create-test"))
		return do_create_test(argc - optind, argv + optind);
	if (!strcmp(cmd, "retrieve-file"))
		return do_retrieve_file(argc - optind, argv + optind);
	if (!strcmp(cmd, "upload-file"))
		return do_upload_file(argc - optind, argv + optind);
	if (!strcmp(cmd, "claim-host"))
		return do_claim_host(argc - optind, argv + optind);
	if (!strcmp(cmd, "create-command"))
		return do_create_command(argc - optind, argv + optind);
	if (!strcmp(cmd, "run-command"))
		return do_run_command(argc - optind, argv + optind);

	fprintf(stderr, "Unsupported command %s\n", cmd);
	goto usage;
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
	} else {
		ni_trace("%s: %s", __func__, ni_dbus_variant_signature(variant));
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


int
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

	list_object = ni_testbus_call_get_object(NULL);
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

int
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

	host_object = ni_testbus_call_create_host(hostname);
	if (host_object == NULL)
		goto out;

	printf("%s\n", host_object->path);
	rv = 0;

out:
	return rv;
}

int
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

	if (!ni_testbus_call_remove_host(hostname))
		return 1;

	return 0;
}

int
do_delete_object(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_CONTEXT };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ "context", required_argument, NULL, OPT_CONTEXT },
		{ NULL }
	};
	ni_dbus_object_t *container_object = NULL, *test_object;
	const char *opt_container = NULL;
	int c, rv = 1;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"wicked [options] delete <object-handle> ...\n"
				"wicked [options] delete --context <object-handle> nickname ...\n"
				"\nSupported options:\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;

		case OPT_CONTEXT:
			opt_container = optarg;
			break;
		}
	}

	if (optind >= argc)
		goto usage;

	if (opt_container) {
		container_object = ni_testbus_call_get_and_refresh_object(opt_container);
	} else {
		int failed = 0;

		while (optind < argc) {
			const char *path = argv[optind++];
			ni_dbus_object_t *object;

			object = ni_testbus_call_get_and_refresh_object(path);
			if (object == NULL) {
				ni_error("no such object %s", path);
				failed++;
				continue;
			}

			if (!ni_testbus_call_delete(object)) {
				ni_error("could not delete object %s", path);
				failed++;
				continue;
			}
		}

		rv = failed? 1 : 0;
	}

out:
	return rv;
}

int
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
	 && !(container_object = ni_testbus_call_get_container(opt_container)))
		return 1;

	test_object = ni_testbus_call_create_test(testname, container_object);
	if (test_object == NULL)
		goto out;

	printf("%s\n", test_object->path);
	rv = 0;

out:
	return rv;
}

int
do_retrieve_file(int argc, char **argv)
{
	enum  { OPT_HELP, };
	static struct option local_options[] = {
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	const char *hostname, *pathname;
	ni_dbus_object_t *agent_object;
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] retrieve-file hostname path\n"
				"\nSupported options:\n"
				"  --help\n"
				"      Show this help text.\n"
				);
			return 1;
		}
	}

	if (optind > argc - 2)
		goto usage;
	hostname = argv[optind++];

	agent_object = ni_testbus_call_get_agent(hostname);
	if (agent_object == NULL)
		return 1;

	ni_debug_wicked("created agent handle");
	while (optind < argc) {
		pathname = argv[optind++];

		ni_testbus_agent_retrieve_file(agent_object, pathname);
	}

	return 0;
}

int
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

		if (optind != argc - 2)
			goto usage;
		local_path = argv[optind++];
		remote_path = argv[optind++];

		agent_object = ni_testbus_call_get_agent(opt_hostname);
		if (agent_object == NULL)
			return 1;

		data = ni_testbus_read_local_file(local_path);
		if (!data)
			return 1;

		ni_fatal("Upload to agent not yet implemented");
	} else {
		const char *local_path, *identifier;
		ni_dbus_object_t *context_object, *file_object;
		ni_buffer_t *data;
		unsigned int count;

		if (optind != argc - 2)
			goto usage;
		local_path = argv[optind++];
		identifier = argv[optind++];

		context_object = ni_testbus_call_get_container(opt_context);
		if (context_object == NULL)
			return 1;

		data = ni_testbus_read_local_file(local_path);
		if (!data)
			return 1;
		count = ni_buffer_count(data);

		file_object = ni_testbus_call_create_tempfile(identifier, NI_TESTBUS_FILE_READ, context_object);
		if (file_object == NULL)
			return 1;

		if (!ni_testbus_call_upload_file(file_object, data))
			return 1;

		printf("Uploaded %u bytes\n", count);
		ni_buffer_free(data);
	}

	return 0;
}

int
do_claim_host(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_HOSTNAME, OPT_CAPABILITY, OPT_ROLE };
	static struct option local_options[] = {
		{ "hostname", required_argument, NULL, OPT_HOSTNAME },
		{ "capability", required_argument, NULL, OPT_CAPABILITY },
		{ "set-role", required_argument, NULL, OPT_ROLE },
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	ni_dbus_object_t *container_object, *host_object;
	const char *opt_hostname = NULL;
	const char *opt_capability = NULL;
	const char *opt_role = NULL;
	int c;

	optind = 1;
	while ((c = getopt_long(argc, argv, "", local_options, NULL)) != EOF) {
		switch (c) {
		default:
		case OPT_HELP:
		usage:
			fprintf(stderr,
				"testbus [options] claim-host --hostname <name> [--set-role <role>] <container-path>\n"
				"testbus [options] claim-host --capability <name> [--set-role <role>] <container-path>\n"
				"\nSupported options:\n"
				"  --hostname <name>\n"
				"      Argument is a hostname, as registered by the agent.\n"
				"  --capability <name>\n"
				"      Argument is a capability string; this will only claim hosts having registered\n"
				"      this capability.\n"
				"      The capability \"any\" will match any host. This is the default if neither this\n"
				"      option nor --hostname is given.\n"
				"  --set-role <role>\n"
				"      This will add the host using the specified role. If no role is given,\n"
				"      it will default to \"testhost\".\n"
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
		}
	}

	if (!opt_role)
		opt_role = "testhost";

	if (opt_hostname && opt_capability) {
		ni_error("you cannot use --hostname and --capability at the same time");
		return 1;
	}

	if (optind != argc - 1) {
		goto usage;
	} else {
		const char *container_path = argv[optind++];

		container_object = ni_testbus_call_get_and_refresh_object(container_path);
		if (container_object == NULL) {
			ni_error("unknown container object %s", container_path);
			return 1;
		}
	}

	if (opt_hostname) {
		host_object = ni_testbus_call_claim_host_by_name(opt_hostname, container_object, opt_role);
	} else {
		if (opt_capability == NULL)
			opt_capability = "any";
		host_object = ni_testbus_call_claim_host_by_capability(opt_capability, container_object, opt_role);
	}

	if (host_object == NULL)
		return 1;

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
__do_create_command(ni_dbus_object_t *container_object, int argc, char **argv, ni_bool_t send_stdin)
{
	ni_string_array_t command_argv = NI_STRING_ARRAY_INIT;
	ni_dbus_object_t *cmd_object;
	int index;

	for (index = 0; index < argc; ++index)
		ni_string_array_append(&command_argv, argv[index]);

	cmd_object = ni_testbus_call_create_command(container_object, &command_argv);
	ni_string_array_destroy(&command_argv);

	if (send_stdin) {
		ni_buffer_t *data;

		data = ni_file_read(stdin);
		ni_testbus_call_command_set_input(cmd_object, data);
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
	ni_testbus_call_create_tempfile("stdout", NI_TESTBUS_FILE_WRITE, cmd_object);
	if (!__samefile(1, 2)) {
		ni_dbus_object_t *file_object;

		file_object = ni_testbus_call_create_tempfile("stderr", NI_TESTBUS_FILE_WRITE, cmd_object);
	}

	return cmd_object;
}

/*
 * Flush an output file of the process to stdout/stderr
 */
static void
flush_process_file(ni_dbus_object_t *proc_object, const char *filename, FILE *ofp)
{
	ni_dbus_object_t *file_object;
	ni_buffer_t *data;

	ni_debug_wicked("%s(%s, %s)", __func__, proc_object->path, filename);
	file_object = ni_testbus_call_container_child_by_name(proc_object,
					ni_testbus_file_class(),
					filename);
	if (file_object == NULL) {
		ni_trace("%s: no file named %s", proc_object->path, filename);
		return;
	}

	ni_trace("%s(%s) -> %s", __func__, filename, file_object->path);

	/* Now download the file */
	data = ni_testbus_call_download_file(file_object);
	if (data == NULL) {
		ni_error("failed to download %s", filename);
		return;
	}

	ni_file_write(ofp, data);
	ni_buffer_free(data);
}

/*
 * Create a command on a given host.
 * This can be executed via run-command, or scheduled for async execution.
 */
int
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
		container_object = ni_testbus_call_get_and_refresh_object(opt_container);
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

	cmd_object = __do_create_command(container_object, argc - optind, argv + optind, FALSE);
	if (cmd_object == NULL)
		return 1;

	printf("%s\n", cmd_object->path);
	return 0;
}

/*
 * Run a command on a given host, block and wait for the result
 */
int
do_run_command(int argc, char **argv)
{
	enum  { OPT_HELP, OPT_HOSTPATH, OPT_CONTEXT, OPT_SEND_STDIN };
	static struct option local_options[] = {
		{ "host", required_argument, NULL, OPT_HOSTPATH },
		{ "context", required_argument, NULL, OPT_CONTEXT },
		{ "send-stdin", no_argument, NULL, OPT_SEND_STDIN },
		{ "help", no_argument, NULL, OPT_HELP },
		{ NULL }
	};
	ni_dbus_object_t *ctxt_object, *host_object, *cmd_object, *proc_object;
	const char *opt_hostpath = NULL;
	const char *opt_contextpath = NULL;
	ni_bool_t opt_send_stdin = FALSE;
	ni_process_exit_info_t exit_info;
	int c;

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
		}
	}

	if (opt_hostpath != 0) {
		host_object = ni_testbus_call_get_and_refresh_object(opt_hostpath);
		if (host_object == NULL) {
			ni_error("unknown host object %s", opt_hostpath);
			return 1;
		}
	} else {
		ni_error("Don't know which host to run this on");
		return 1;
	}

	if (opt_contextpath != 0) {
		ctxt_object = ni_testbus_call_get_and_refresh_object(opt_contextpath);
		if (ctxt_object == NULL) {
			ni_error("unknown context object %s", opt_contextpath);
			return 1;
		}
	} else {
		ctxt_object = host_object;
	}

	if (optind > argc - 1)
		goto usage;

	cmd_object = __do_create_command(ctxt_object, argc - optind, argv + optind, opt_send_stdin);
	if (!cmd_object)
		return 1;

	proc_object = ni_testbus_call_host_run(host_object, cmd_object);
	if (!proc_object)
		return 1;

	/* The above should return an object handle for the process, and
	 * register a waitq entry that will catch the processExit signals
	 * emitted by the master.
	 *
	 * Wait for the command to complete, and process its exit information
	 */
	if (!ni_testbus_wait_for_process(proc_object, -1, &exit_info)) {
		ni_error("failed to wait for process to complete");
		return 1;
	}

	if (exit_info.stdout_bytes)
		flush_process_file(proc_object, "stdout", stdout);
	if (exit_info.stderr_bytes)
		flush_process_file(proc_object, "stderr", stderr);

	ni_testbus_call_delete(proc_object);
	ni_testbus_call_delete(cmd_object);

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

	default:
		ni_error("process disappeared into Nirvana");
		return 1;
	}

	return 0;
}

ni_buffer_t *
ni_testbus_read_local_file(const char *filename)
{
	ni_buffer_t *result;
	FILE *fp;

	if (!(fp = fopen(filename, "r"))) {
		ni_error("unable to open %s: %m", filename);
		return NULL;
	}

	result = ni_file_read(fp);
	fclose(fp);

	return result;
}
