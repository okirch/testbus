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
#include "files.h"

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
	char *			hostname;
	ni_uuid_t		uuid;
	ni_string_array_t	capabilities;
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
			if (ni_string_eq(c->name, "capability"))
				ni_string_array_append(&state->capabilities, c->cdata);
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

/*
 * After the process has finished, upload the output
 */
static void
ni_testbus_agent_upload_output(ni_dbus_object_t *proc_object, const char *filename, ni_buffer_chain_t **chain)
{
	ni_dbus_object_t *file_object;
	ni_buffer_t *bp;

	if (ni_buffer_chain_count(*chain) == 0)
		return;

	file_object = ni_testbus_call_create_tempfile(filename, proc_object);
	if (file_object == NULL)
		goto failed;

	while ((bp = ni_buffer_chain_get_next(chain)) != NULL) {
		if (!ni_testbus_call_upload_file(file_object, bp)) {
			ni_buffer_free(bp);
			goto failed;
		}
		ni_buffer_free(bp);
	}

	return;

failed:
	ni_error("%s: failed to upload %s", proc_object->path, filename);
	return;
}

/*
 * Callback function for processes
 */
struct __ni_testbus_process_context {
	ni_dbus_server_t *	server;
	char *			object_path;
	ni_buffer_chain_t *	stdout_buffers;
	ni_buffer_chain_t *	stderr_buffers;
};

static struct __ni_testbus_process_context *
__ni_testbus_process_context_new(const char *master_object_path)
{
	struct __ni_testbus_process_context *ctx;

	ctx = ni_calloc(1, sizeof(*ctx));
//	ctx->server = ni_dbus_object_get_server(object);
	ctx->object_path = ni_strdup(master_object_path);
	return ctx;
}

static void
__ni_testbus_process_context_free(struct __ni_testbus_process_context *ctx)
{
	ni_buffer_chain_discard(&ctx->stdout_buffers);
	ni_buffer_chain_discard(&ctx->stderr_buffers);
	ni_string_free(&ctx->object_path);
	free(ctx);
}

static void
__ni_testbus_process_exit_notify(ni_process_t *pi)
{
	struct __ni_testbus_process_context *ctx = pi->user_data;
	ni_process_exit_info_t exit_info;
	ni_dbus_object_t *proc_object;

	ni_trace("process %s exited", ctx->object_path);
	ni_process_get_exit_info(pi, &exit_info);

	proc_object = ni_testbus_call_get_and_refresh_object(ctx->object_path);

	ni_testbus_agent_upload_output(proc_object, "stdout", &ctx->stdout_buffers);
	ni_testbus_agent_upload_output(proc_object, "stderr", &ctx->stderr_buffers);

	ni_testbus_call_process_exit(proc_object, &exit_info);

	__ni_testbus_process_context_free(ctx);
	pi->user_data = NULL;
}

static void
__ni_testbus_process_read_notify(ni_process_t *pi, int fd, ni_buffer_t *bp)
{
	struct __ni_testbus_process_context *ctx = pi->user_data;
	ni_buffer_chain_t **chain = NULL;

	ni_trace("%s(%u, %d, %u)", __func__, pi->pid, fd, ni_buffer_count(bp));
	if (fd == 1)
		chain = &ctx->stdout_buffers;
	else if (fd == 2)
		chain = &ctx->stderr_buffers;

	if (chain != NULL)
		ni_buffer_chain_append(chain, bp);
	else
		ni_buffer_free(bp);

	/* Future extension: signal the master that we have data.
	 * This would allow continuous streaming of the process output,
	 * rather than transferring everything in bulk on process exit. */
}

static ni_bool_t
__ni_testbus_process_run(ni_process_t *pi, const char *master_object_path, ni_testbus_file_array_t *files)
{
	struct __ni_testbus_process_context *ctx;

	if (files && !ni_testbus_agent_process_attach_files(pi, files)) {
		ni_error("process %u: failed to attach files", pi->pid);
		return FALSE;
	}

	if (ni_process_run(pi) < 0)
		return FALSE;

	ctx = __ni_testbus_process_context_new(master_object_path);
	pi->notify_callback = __ni_testbus_process_exit_notify;
	pi->read_callback = __ni_testbus_process_read_notify;
	pi->user_data = ctx;

	return TRUE;
}


/*
 * Process signals from master
 */
static void
__ni_testbus_agent_process_host_signal(ni_dbus_connection_t *connection, ni_dbus_message_t *msg, void *user_data)
{
	const char *signal_name = dbus_message_get_member(msg);
	const char *object_path = dbus_message_get_path(msg);
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

		ni_trace("received signal %s from %s", signal_name, object_path);

		if (!__ni_testbus_process_run(pi, object_path, files)) {
			ni_process_exit_info_t exit_info = { .how = NI_PROCESS_NONSTARTER };

			/* FIXME: notify master that we failed to fork */
			ni_process_free(pi);
		}
		ni_testbus_file_array_free(files);
	}

out:
	ni_dbus_variant_vector_destroy(argv, 2);
}

static void
ni_testbus_agent_setup_signals(ni_dbus_client_t *client)
{
	ni_dbus_client_add_signal_handler(client,
			NI_TESTBUS_DBUS_BUS_NAME,		/* sender */
			NULL,					/* path */
			NI_TESTBUS_HOST_INTERFACE,		/* interface */
			__ni_testbus_agent_process_host_signal,
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

	if (!ni_objectmodel_register(&ni_testbus_agent_objectmodel))
		ni_fatal("Cannot initialize objectmodel, giving up.");

	dbus_server = ni_objectmodel_create_server();
	if (!dbus_server)
		ni_fatal("Cannot create server, giving up.");

	dbus_client = ni_dbus_server_create_shared_client(dbus_server, NI_TESTBUS_DBUS_BUS_NAME);
	ni_call_init_client(dbus_client);

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

	ni_testbus_agent_setup_signals(dbus_client);

	if (!ni_testbus_agent_add_capabilities(host_object, &state->capabilities))
		ni_fatal("failed to register agent capabilities");

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
