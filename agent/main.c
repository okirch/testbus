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
#include <ctype.h>

#include <dborb/netinfo.h>
#include <dborb/logging.h>
#include <dborb/socket.h>
#include <dborb/dbus-errors.h>
#include <dborb/dbus-model.h>
#include <dborb/process.h>
#include <dborb/xml.h>
#include <dborb/buffer.h>
#include <testbus/model.h>
#include <testbus/client.h>
#include <testbus/process.h>
#include <testbus/file.h>

#include "dbus-filesystem.h"
#include "monitor.h"
#include "files.h"

#define APP_IDENTITY		"agent"

enum {
	OPT_HELP,
	OPT_VERSION,
	OPT_CONFIGFILE,
	OPT_DEBUG,
	OPT_LOG_LEVEL,
	OPT_LOG_TARGET,

	OPT_FOREGROUND,
	OPT_DBUS_SOCKET,

	OPT_RECONNECT,
	OPT_ALLOW_SHUTDOWN,
	OPT_PUBLISH,
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
	{ "dbus-socket",	required_argument,	NULL,	OPT_DBUS_SOCKET },

	/* specific */
	{ "reconnect",		no_argument,		NULL,	OPT_RECONNECT },
	{ "allow-shutdown",	no_argument,		NULL,	OPT_ALLOW_SHUTDOWN },
	{ "publish",		required_argument,	NULL,	OPT_PUBLISH },

	{ NULL }
};

typedef struct ni_testbus_agent_state {
	char *			hostname;
	ni_string_array_t	capabilities;
	ni_var_array_t		environ;
} ni_testbus_agent_state_t;

static const char *	program_name;
static const char *	opt_log_level;
static const char *	opt_log_target;
static int		opt_foreground;
static const char *	opt_state_file;
char *			opt_dbus_socket;
char *			opt_hostname;
static ni_bool_t	opt_reconnect;
static ni_bool_t	opt_allow_shutdown;

static ni_testbus_agent_state_t ni_testbus_agent_global_state;

static void		ni_testbus_agent_publish_file(ni_testbus_agent_state_t *, const char *path);
static void		ni_testbus_agent_state_setcap(ni_testbus_agent_state_t *, const char *name);
static void		ni_testbus_agent_state_setenv(ni_testbus_agent_state_t *, const char *name, const char *value);
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
				"testbus-agent [options] [publish-info ...]\n"
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
				"  --reconnect\n"
				"        Rather than trying a fresh agent registration, use the information\n"
				"        from the state file to attempt a reconnect.\n"
				"  --foreground\n"
				"        Do not background the service.\n"
				"  --dbus-socket <path>\n"
				"        Connect to the specified DBus socket rather than the default dbus system bus.\n"
				"  --allow-shutdown\n"
				"        When receiving a request to shutdown or reboot, do shutdown the host.\n"
				"        The default is for testbus-agent to merely exit.\n"
				"  --publish <path>\n"
				"        Publish the capabilities and environment variables specified in\n"
				"        the file specified by <path>.\n"
				"\n"
				"Additional parameters specify environment variables or capabilities to publish:\n"
				"  capability <name>\n"
				"        Adds <name> to the list of capabilities supported by this agent\n"
				"  setenv <name> <value>\n"
				"        Adds <name> to the list of environment variables and assign <value>\n"
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

		case OPT_DBUS_SOCKET:
			opt_dbus_socket = optarg;
			break;

		case OPT_RECONNECT:
			opt_reconnect = TRUE;
			break;

		case OPT_ALLOW_SHUTDOWN:
			opt_allow_shutdown = TRUE;
			break;

		case OPT_PUBLISH:
			ni_testbus_agent_publish_file(&ni_testbus_agent_global_state, optarg);
			break;
		}
	}

	if (optind < argc) {
		while (optind < argc) {
			const char *kwd = argv[optind++];

			if (ni_string_eq(kwd, "capability")) {
				if (optind >= argc)
					goto usage;
				ni_testbus_agent_state_setcap(&ni_testbus_agent_global_state, argv[optind++]);
			} else
			if (ni_string_eq(kwd, "setenv")) {
				const char *name, *value;

				if (optind + 1 >= argc)
					goto usage;
				name = argv[optind++];
				value = argv[optind++];
				ni_testbus_agent_state_setenv(&ni_testbus_agent_global_state, name, value);
			} else {
				ni_fatal("unknown keyword \"%s\" on command line", kwd);
			}
		}
	}

	if (ni_init(APP_IDENTITY) < 0)
		return 1;

	if (opt_dbus_socket)
		ni_config_set_dbus_socket_path(opt_dbus_socket);

	if (ni_server_is_running(APP_IDENTITY)) {
		ni_error("another testbus %s seems to be running already - refusing to start", APP_IDENTITY);
		return 1;
	}

	if (opt_log_target == NULL) {
		ni_log_destination_default(program_name, opt_foreground);
	} else
	if (!ni_log_destination(program_name, opt_log_target)) {
		fprintf(stderr, "Bad log destination \%s\"\n", opt_log_target);
		return 1;
	}

	ni_testbus_agent(&ni_testbus_agent_global_state);
	return 0;
}

static inline char *
__next_token(char **pos)
{
	char *s, *ret;

	s = *pos;
	while (isspace(*s))
		++s;

	ret = s++;
	while (*s && !isspace(*s))
		++s;
	if (*s)
		*s++ = '\0';

	*pos = s;

	if (*ret == '\0' || *ret == '#')
		return NULL;

	return ret;
}

static void
ni_testbus_agent_publish_file(ni_testbus_agent_state_t *state, const char *path)
{
	FILE *fp;
	char buf[512];
	unsigned int lineno = 0;

	if (!(fp = fopen(path, "r")))
		ni_fatal("unable to open %s: %m", path);

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		char *pos = buf, *kwd, *name, *value;

		++lineno;

		if (!(kwd = __next_token(&pos)))
			continue;

		if (ni_string_eq(kwd, "capability")) {
			while ((name = __next_token(&pos)) != NULL)
				ni_testbus_agent_state_setcap(state, name);
		} else if (ni_string_eq(kwd, "setenv")) {
			if (!(name = __next_token(&pos)))
				ni_fatal("%s, line %u: setenv without variable name", path, lineno);

			value = ni_unquote((const char **) &pos, " \t\r\n");
			if (value == NULL || *value == '#')
				ni_fatal("%s, line %u: setenv without variable value", path, lineno);
			if (__next_token(&pos))
				ni_fatal("%s, line %u: garbage after setenv variable value", path, lineno);

			ni_testbus_agent_state_setenv(state, name, value);
			free(value); /* ni_quote returns an allocated string */
		} else {
			ni_fatal("%s, line %u: unknown keyword \"%s\"", path, lineno, kwd);
		}
	}
	fclose(fp);
}

static void
ni_testbus_agent_state_setcap(ni_testbus_agent_state_t *state, const char *name)
{
	ni_string_array_append(&state->capabilities, name);
}

static void
ni_testbus_agent_state_setenv(ni_testbus_agent_state_t *state, const char *name, const char *value)
{
	ni_var_array_set(&state->environ, name, value);
}

static const char *
ni_testbus_agent_state_file_path(void)
{
	static char state_file_pathbuf[PATH_MAX];

	if (opt_state_file == NULL) {
		snprintf(state_file_pathbuf, sizeof(state_file_pathbuf),
				"%s/state.xml", ni_config_statedir());
		opt_state_file = state_file_pathbuf;
	}

	ni_debug_testbus("State file is %s", opt_state_file);
	return opt_state_file;
}

void
ni_testbus_agent_read_state(ni_testbus_agent_state_t *state)
{
	const char *state_file;
	xml_document_t *doc;
	xml_node_t *root, *node;

	state_file = ni_testbus_agent_state_file_path();
	if (!ni_file_exists(state_file)) {
		ni_debug_testbus("State file does not exist");
		return;
	}

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
			if (ni_string_eq(c->name, "capability"))
				ni_string_array_append(&state->capabilities, c->cdata);
			else
				ni_warn("%s: ignoring unknown XML element <%s>",
						xml_node_location(c), c->name);
		}
	}

	xml_document_free(doc);

	ni_debug_testbus("Successfully read state from %s", state_file);
}

void
ni_testbus_agent_write_state(const ni_testbus_agent_state_t *state)
{
	const char *state_file;
	xml_document_t *doc;
	xml_node_t *root, *node;

	doc = xml_document_new();
	root = xml_document_root(doc);

	node = xml_node_new("state", root);
	xml_node_new_element("hostname", node, state->hostname);

	state_file = ni_testbus_agent_state_file_path();
	if (xml_document_write(doc, state_file) < 0)
		ni_error("unable to write status file %s", state_file);
	else
		ni_debug_testbus("Wrote agent state to %s.", state_file);

	xml_document_free(doc);
}

void
ni_testbus_agent_init_monitors(ni_dbus_object_t *host_object)
{
	ni_eventlog_t *log;
	ni_monitor_t *mon;

	ni_testbus_agent_eventlog_init(host_object);

	log = ni_testbus_agent_eventlog();

	if ((mon = ni_agent_create_syslog_monitor(log)) != NULL) {
		ni_testbus_agent_register_monitor(mon);
		ni_monitor_put(mon);
	}
}

/*
 * Process signals from master
 */
static void
__ni_testbus_agent_process_host_signal(ni_dbus_connection_t *connection, ni_dbus_message_t *msg, void *user_data)
{
	const char *signal_name = dbus_message_get_member(msg);
	ni_dbus_variant_t argv[2];
	int argc;

	if (!signal_name)
		return;

	ni_dbus_variant_vector_init(argv, 2);

	argc = ni_dbus_message_get_args_variants(msg, argv, 2);
	if (argc < 0) {
		ni_error("%s: cannot extract parameters of signal %s", __func__, signal_name);
		goto out;
	}

	if (ni_string_eq(signal_name, "processScheduled")) {
		const char *object_path;
		ni_process_t *pi = NULL;
		ni_testbus_file_array_t *files = NULL;

		if (argc < 2
		 || !(pi = ni_testbus_process_deserialize(&argv[0]))
		 || !ni_dbus_dict_get_string(&argv[0], "object-path", &object_path)
		 || !(files = ni_testbus_file_array_deserialize(&argv[1]))) {
			if (pi)
				ni_process_free(pi);
			ni_error("%s: bad argument for signal %s()", __func__, signal_name);
			goto out;
		}

		ni_debug_testbus("received signal %s(%s)", signal_name, object_path);

		ni_testbus_agent_run_command(pi, object_path, files);
	} else
	if (ni_string_eq(signal_name, "shutdownRequested")) {
		ni_debug_testbus("received signal %s", signal_name);

		if (!opt_allow_shutdown) {
			ni_note("exiting due to shutdownRequested() signal");
			exit(0);
		}

		execl("/sbin/shutdown", "shutdown", "-h", "now", NULL);
		ni_fatal("unable to execute /sbin/shutdown: %m");
	} else
	if (ni_string_eq(signal_name, "rebootRequested")) {
		ni_debug_testbus("received signal %s", signal_name);

		if (!opt_allow_shutdown) {
			ni_note("exiting due to rebootRequested() signal");
			exit(0);
		}

		execl("/sbin/reboot", "reboot", NULL);
		ni_fatal("unable to execute /sbin/reboot: %m");
	}

out:
	ni_dbus_variant_vector_destroy(argv, 2);
}

static void
__ni_testbus_agent_process_container_signal(ni_dbus_connection_t *connection, ni_dbus_message_t *msg, void *user_data)
{
	const char *signal_name = dbus_message_get_member(msg);
	const char *object_path = dbus_message_get_path(msg);

	if (!signal_name)
		return;

	if (ni_string_eq(signal_name, "deleted")) {
		ni_debug_testbus("received signal %s from %s", signal_name, object_path);
	}
}

static void
__ni_testbus_agent_process_file_signal(ni_dbus_connection_t *connection, ni_dbus_message_t *msg, void *user_data)
{
	const char *signal_name = dbus_message_get_member(msg);
	const char *object_path = dbus_message_get_path(msg);

	if (!signal_name)
		return;

	if (ni_string_eq(signal_name, "deleted")) {
		ni_debug_testbus("received signal %s from %s", signal_name, object_path);
		ni_testbus_agent_discard_cached_file(object_path);
	}
}

static void
ni_testbus_agent_setup_signals(ni_dbus_client_t *client, ni_dbus_object_t *host_object)
{
	ni_dbus_client_add_signal_handler(client,
			NI_TESTBUS_DBUS_BUS_NAME,		/* sender */
			host_object->path,			/* path */
			NI_TESTBUS_HOST_INTERFACE,		/* interface */
			__ni_testbus_agent_process_host_signal,
			NULL);

	ni_dbus_client_add_signal_handler(client,
			NI_TESTBUS_DBUS_BUS_NAME,		/* sender */
			NULL,					/* path */
			NI_TESTBUS_CONTAINER_INTERFACE,		/* interface */
			__ni_testbus_agent_process_container_signal,
			NULL);

	ni_dbus_client_add_signal_handler(client,
			NI_TESTBUS_DBUS_BUS_NAME,		/* sender */
			NULL,					/* path */
			NI_TESTBUS_TMPFILE_INTERFACE,		/* interface */
			__ni_testbus_agent_process_file_signal,
			NULL);
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
	ni_dbus_client_t *dbus_client;

	ni_testbus_agent_read_state(state);

	if (state->hostname == NULL) {
		char hostname[HOST_NAME_MAX];

		if (gethostname(hostname, sizeof(hostname)) < 0)
			ni_fatal("unable to get hostname");
		ni_string_dup(&state->hostname, hostname);
	}

	if (ni_debug & NI_TRACE_TESTBUS) {
		ni_trace("Agent state");
		ni_trace("Hostname:     %s", state->hostname);

		if (state->capabilities.count == 0) {
			ni_trace("Capabilities: none");
		} else {
			ni_stringbuf_t sb = NI_STRINGBUF_INIT_DYNAMIC;
			unsigned int i;

			for (i = 0; i < state->capabilities.count; ++i) {
				if (i)
					ni_stringbuf_putc(&sb, ' ');
				ni_stringbuf_puts(&sb, state->capabilities.data[i]);
			}
			ni_trace("Capabilities: %s", sb.string);
			ni_stringbuf_destroy(&sb);
		}

		if (state->environ.count == 0) {
			ni_trace("Environment:  none");
		} else {
			unsigned int i;

			ni_trace("Environment:  %u variables", state->environ.count);
			for (i = 0; i < state->environ.count; ++i) {
				ni_var_t *vp = &state->environ.data[i];

				ni_trace("              %s=\"%s\"", vp->name, vp->value);
			}
		}
	}

	if (!ni_objectmodel_register(&ni_testbus_agent_objectmodel))
		ni_fatal("Cannot initialize objectmodel, giving up.");

	dbus_server = ni_objectmodel_create_server();
	if (!dbus_server)
		ni_fatal("Cannot create server, giving up.");

	dbus_client = ni_dbus_server_create_shared_client(dbus_server, NI_TESTBUS_DBUS_BUS_NAME);
	ni_testbus_client_init(dbus_client);

	ni_debug_testbus("Testbus agent starting");
	if (!opt_reconnect) {
		host_object = ni_testbus_client_create_host(state->hostname);
		/* FIXME: set the drop-on-disconnect property */
	} else {
		host_object = ni_testbus_client_reconnect_host(state->hostname);
	}

	if (host_object == NULL)
		ni_fatal("unable to register agent name \"%s\"", state->hostname);
	ni_debug_testbus("registered agent as host %s", host_object->path);

	ni_testbus_agent_setup_signals(dbus_client, host_object);

	if (!ni_testbus_agent_add_capabilities(host_object, &state->capabilities))
		ni_fatal("failed to register agent capabilities");

	if (!ni_testbus_agent_add_environment(host_object, &state->environ))
		ni_fatal("failed to publish agent environment");

	ni_testbus_agent_init_monitors(host_object);

	if (!opt_foreground && ni_server_background(APP_IDENTITY) < 0)
		ni_fatal("unable to background testbus agent");

	/* Inform master that we're ready to serve requests */
	ni_dbus_server_send_signal(dbus_server,
			ni_dbus_server_get_root_object(dbus_server),
			NI_TESTBUS_AGENT_INTERFACE,
			"ready",
			0, NULL);

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
