/*
 * Copyright (C) 2013 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <mcheck.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>

#include <dborb/netinfo.h>
#include <dborb/logging.h>
#include <dborb/socket.h>
#include <testbus/model.h>
#include <testbus/client.h>
#include <dborb/dbus-errors.h>
#include <dborb/dbus-model.h>
#include <dborb/xml.h>
#include "dbus-filesystem.h"

enum {
	OPT_HELP,
	OPT_VERSION,
	OPT_CONFIGFILE,
	OPT_DEBUG,
	OPT_LOG_LEVEL,
	OPT_LOG_TARGET,

	OPT_FOREGROUND,

	OPT_DRYRUN,
	OPT_ROOTDIR,
	OPT_RECONNECT,
};

static struct option	options[] = {
	/* common */
	{ "help",		no_argument,		NULL,	OPT_HELP },
	{ "version",		no_argument,		NULL,	OPT_VERSION },
	{ "config",		required_argument,	NULL,	OPT_CONFIGFILE },
	{ "debug",		required_argument,	NULL,	OPT_DEBUG },
	{ "log-level",		required_argument,	NULL,	OPT_LOG_LEVEL },
	{ "log-target",		required_argument,	NULL,	OPT_LOG_TARGET },

	/* daemon */
	{ "foreground",		no_argument,		NULL,	OPT_FOREGROUND },

	/* specific */
	{ "dryrun",		no_argument,		NULL,	OPT_DRYRUN },
	{ "dry-run",		no_argument,		NULL,	OPT_DRYRUN },
	{ "root-directory",	required_argument,	NULL,	OPT_ROOTDIR },
	{ "reconnect",		no_argument,		NULL,	OPT_RECONNECT },

	{ NULL }
};

typedef struct ni_testbus_agent_state {
	char *		hostname;
	ni_uuid_t	uuid;
} ni_testbus_agent_state_t;

static const char *	program_name;
static const char *	opt_log_level;
static const char *	opt_log_target;
static int		opt_foreground;
static const char *	opt_state_file;
int			opt_global_dryrun;
char *			opt_global_rootdir;
char *			opt_hostname;
static int		opt_reconnect;

static ni_testbus_agent_state_t ni_testbus_agent_global_state;

static void		ni_testbus_agent_read_state(const char *state_file, ni_testbus_agent_state_t *);
static void		ni_testbus_agent_write_state(const char *state_file, const ni_testbus_agent_state_t *);
static void		ni_testbus_agent(ni_testbus_agent_state_t *state);

int
main(int argc, char **argv)
{
	int c;

	mtrace();

	program_name = ni_basename(argv[0]);
	while ((c = getopt_long(argc, argv, "+", options, NULL)) != EOF) {
		switch (c) {
		case OPT_HELP:
		default:
		usage:
			fprintf(stderr,
				"testbus-agent [options]\n"
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
				"  ... tbd ...\n"
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

		case OPT_FOREGROUND:
			opt_foreground = 1;
			break;

		case OPT_DRYRUN:
			opt_global_dryrun = 1;
			break;

		case OPT_ROOTDIR:
			opt_global_rootdir = optarg;
			break;

		case OPT_RECONNECT:
			opt_reconnect = 1;
			break;
		}
	}

	if (optind < argc)
		goto usage;

	if (opt_log_target) {
		if (!ni_log_destination(program_name, opt_log_target)) {
			fprintf(stderr, "Bad log destination \%s\"\n",
				opt_log_target);
			return 1;
		}
	} else if (getppid() != 1) {
		ni_log_destination(program_name, "syslog:perror:user");
	} else {
		ni_log_destination(program_name, "syslog::user");
	}

	if (ni_init("agent") < 0)
		return 1;

	if (opt_state_file == NULL) {
		static char dirname[PATH_MAX];

		snprintf(dirname, sizeof(dirname), "%s/state.xml", ni_config_statedir());
		opt_state_file = dirname;

		ni_debug_wicked("State file is %s", opt_state_file);
		if (ni_file_exists(opt_state_file))
			ni_testbus_agent_read_state(opt_state_file, &ni_testbus_agent_global_state);
	}

	ni_testbus_agent(&ni_testbus_agent_global_state);
	return 0;
}

void
ni_testbus_agent_read_state(const char *state_file, ni_testbus_agent_state_t *state)
{
	xml_document_t *doc;
	xml_node_t *root, *node;

	doc = xml_document_read(state_file);
	if (doc == NULL)
		ni_fatal("unable to read state file %s", state_file);

	if ((root = xml_document_root(doc)) != NULL
	 && (node = xml_node_get_child(root, "state")) != NULL) {
		xml_node_t *c;

		for (c = node->children; c; c = c->next) {
			if (ni_string_eq(c->name, "hostname"))
				ni_string_dup(&state->hostname, c->cdata);
			else
			if (ni_string_eq(c->name, "uuid"))
				ni_uuid_parse(&state->uuid, c->cdata);
			else
				ni_warn("%s: ignoring unknown XML element <%s>",
						xml_node_location(c), c->name);
		}
	}

	xml_document_free(doc);
}

void
ni_testbus_agent_write_state(const char *state_file, const ni_testbus_agent_state_t *state)
{
	xml_document_t *doc;
	xml_node_t *root, *node;

	doc = xml_document_new();
	root = xml_document_root(doc);

	node = xml_node_new("state", root);
	xml_node_new_element("hostname", node, state->hostname);
	xml_node_new_element("uuid", node, ni_uuid_print(&state->uuid));

	if (xml_document_write(doc, state_file) < 0)
		ni_error("unable to write status file %s", state_file);
	else
		ni_debug_wicked("Wrote agent state to %s.", state_file);

	xml_document_free(doc);
}

static void
ni_testbus_agent_bind_builtin()
{
	ni_testbus_bind_builtin_filesystem();
}

static void
ni_testbus_agent_create_static_objects(ni_dbus_server_t *server)
{
	ni_testbus_create_static_objects_filesystem(server);
}


ni_dbus_objectmodel_t	ni_testbus_agent_objectmodel = {
	.bus_name_prefix	= NI_TESTBUS_NAMESPACE ".Agent",
	.root_object_path	= NI_TESTBUS_ROOT_PATH,
	.root_interface_name	= NI_TESTBUS_ROOT_INTERFACE,

	.bind_builtin		= ni_testbus_agent_bind_builtin,
	.create_static_objects	= ni_testbus_agent_create_static_objects,
};

void
ni_testbus_agent(ni_testbus_agent_state_t *state)
{
	ni_dbus_server_t *dbus_server;
	ni_dbus_object_t *host_object;

	if (!ni_objectmodel_register(&ni_testbus_agent_objectmodel))
		ni_fatal("Cannot initialize objectmodel, giving up.");

	dbus_server = ni_objectmodel_create_server();
	if (!dbus_server)
		ni_fatal("Cannot create server, giving up.");

	ni_call_init_client(ni_dbus_server_create_shared_client(dbus_server, NI_TESTBUS_DBUS_BUS_NAME));

	ni_trace("Testbus agent starting");
	if (state->hostname == NULL || !opt_reconnect) {
		char hostname[HOST_NAME_MAX];

		if (gethostname(hostname, sizeof(hostname)) < 0)
			ni_fatal("unable to get hostname");
		ni_string_dup(&state->hostname, hostname);

		host_object = ni_testbus_call_create_host(hostname);
	} else {
		host_object = ni_testbus_call_reconnect_host(state->hostname, &state->uuid);
	}

	if (host_object == NULL)
		ni_fatal("unable to register agent name \"%s\"", state->hostname);
	ni_debug_wicked("registered agent as host %s", host_object->path);

	{
		const ni_dbus_variant_t *var = NULL;

		var = ni_dbus_object_get_cached_property(host_object, "uuid",
				ni_dbus_object_get_service(host_object, NI_TESTBUS_HOST_INTERFACE));
		if (var == NULL || !ni_dbus_variant_get_uuid(var, &state->uuid)) {
			ni_warn("could not get host registration uuid");
			ni_trace("var=%p", var);
		} else {
			ni_testbus_agent_write_state(opt_state_file, state);
		}
	}

#if 0
	if (!opt_foreground) {
		if (ni_server_background(program_name) < 0)
			ni_fatal("unable to background server");
	}
#endif

	while (!ni_caught_terminal_signal()) {
		long timeout;

		do {
			timeout = ni_timer_next_timeout();
		} while (ni_dbus_objects_garbage_collect());

		if (ni_socket_wait(timeout) < 0)
			ni_fatal("ni_socket_wait failed");
	}

	exit(0);
}
