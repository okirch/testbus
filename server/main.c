/*
 * testbusd main function
 *
 * Based on the wickedd main function.
 *
 * Copyright (C) 2010-2013 Olaf Kirch <okir@suse.de>
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
#include <getopt.h>
#include <limits.h>
#include <errno.h>

#include <dborb/netinfo.h>
#include <dborb/logging.h>
#include <dborb/socket.h>
#include <dborb/dbus-model.h>
#include <testbus/model.h>
#include "model.h"
#include "container.h"

#define APP_IDENTITY		"master"

extern ni_dbus_objectmodel_t	ni_testbus_objectmodel;

enum {
	OPT_HELP,
	OPT_VERSION,
	OPT_CONFIGFILE,
	OPT_DEBUG,
	OPT_LOG_LEVEL,
	OPT_LOG_TARGET,

	OPT_FOREGROUND,
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

	{ NULL }
};

static const char *	program_name;
static const char *	opt_log_level;
static const char *	opt_log_target;
static int		opt_foreground;
static ni_dbus_server_t *dbus_server;

static void		ni_testbus_master(void);
static void		__ni_testbus_dbus_bus_signal_handler(ni_dbus_connection_t *, ni_dbus_message_t *, void *);
static void		handle_other_event(ni_event_t);

int
main(int argc, char **argv)
{
	int c;

	program_name = ni_basename(argv[0]);

	while ((c = getopt_long(argc, argv, "+", options, NULL)) != EOF) {
		switch (c) {
		default:
		usage:
		case OPT_HELP:
			fprintf(stderr,
				"%s [options]\n"
				"This command understands the following options\n"
				"  --help\n"
				"  --version\n"
				"  --config filename\n"
				"        Read configuration file <filename> instead of system default.\n"
				"  --debug facility\n"
				"        Enable debugging for debug <facility>.\n"
				"        Use '--debug help' for a list of debug facilities.\n"
				"  --log-level level\n"
				"        Set log level to <error|warning|notice|info|debug>.\n"
				"  --log-target target\n"
				"        Set log destination to <stderr|syslog>.\n"
				"  --foreground\n"
				"        Tell the daemon to not background itself at startup.\n"
				, program_name);
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

		case OPT_LOG_LEVEL:
			opt_log_level = optarg;
			if (!ni_log_level_set(optarg)) {
				fprintf(stderr, "Bad log level \%s\"\n", optarg);
				return 1;
			}
			break;

		case OPT_LOG_TARGET:
			opt_log_target = optarg;
			break;

		case OPT_FOREGROUND:
			opt_foreground = 1;
			break;
		}
	}

	if (optind != argc)
		goto usage;

	if (ni_init(APP_IDENTITY) < 0)
		return 1;

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

	ni_testbus_master();
	return 0;
}

static void
ni_testbus_bind_builtin()
{
	ni_testbus_bind_builtin_environ();
	ni_testbus_bind_builtin_command();
	ni_testbus_bind_builtin_host();
	ni_testbus_bind_builtin_file();
	ni_testbus_bind_builtin_test();
	ni_testbus_bind_builtin_process();
	ni_testbus_bind_builtin_container();
}

static void
ni_testbus_create_static_objects(ni_dbus_server_t *server)
{
	ni_testbus_create_static_objects_host(server);
	ni_testbus_create_static_objects_container(server);
//	ni_testbus_create_static_objects_environ(server);
//	ni_testbus_create_static_objects_command(server);
	ni_testbus_create_static_objects_file(server);
	ni_testbus_create_static_objects_test(server);
}

/*
 * Implement service for configuring the system's network interfaces
 */
void
ni_testbus_master(void)
{
	ni_xs_scope_t *	schema;

	ni_testbus_objectmodel.bind_builtin = ni_testbus_bind_builtin;
	ni_testbus_objectmodel.create_static_objects = ni_testbus_create_static_objects;
	if (!ni_objectmodel_register(&ni_testbus_objectmodel))
		ni_fatal("Cannot initialize objectmodel, giving up.");
	schema = ni_objectmodel_get_schema();
	(void) schema; /* shut up */

	dbus_server = ni_objectmodel_create_server();
	if (!dbus_server)
		ni_fatal("Cannot create server, giving up.");

	/* Listen for other events */
	ni_server_listen_other_events(handle_other_event);

	/* sender=org.freedesktop.DBus -> dest=(null destination) path=/org/freedesktop/DBus; interface=org.freedesktop.DBus; member=NameOwnerChanged */
	ni_dbus_server_add_signal_handler(dbus_server,
			"org.freedesktop.DBus",		/* sender*/
			"/org/freedesktop/DBus",	/* object path */
			"org.freedesktop.DBus",		/* interface */
			__ni_testbus_dbus_bus_signal_handler, NULL);
		

	if (!opt_foreground && ni_server_background(APP_IDENTITY) < 0)
		ni_fatal("unable to background testbus master");

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

void
__ni_testbus_dbus_bus_signal_handler(ni_dbus_connection_t *connection, ni_dbus_message_t *msg, void *user_data)
{
	const char *signal_name = dbus_message_get_member(msg);

	if (ni_string_eq(signal_name, "NameOwnerChanged")) {
		const char *name, *old_owner, *new_owner;
		DBusError error = DBUS_ERROR_INIT;
		dbus_bool_t rv;

		rv = dbus_message_get_args(msg, &error,
				DBUS_TYPE_STRING, &name,
				DBUS_TYPE_STRING, &old_owner,
				DBUS_TYPE_STRING, &new_owner,
				DBUS_TYPE_INVALID);
		if (!rv) {
			ni_error("NameOwnerChanged: cannot decode args");
			return;
		}

		if (!strncmp(name, NI_TESTBUS_NAMESPACE, sizeof(NI_TESTBUS_NAMESPACE)-1))
			ni_testbus_record_wellknown_bus_name(name, new_owner);

		if (new_owner == NULL || new_owner[0] == '\0')
			ni_testbus_container_notify_agent_exit(ni_testbus_global_context(), name);
	}
}

/*
 * Map well-known DBus names to their owner (by listening for NameOwnerChanged events
 */
typedef struct ni_name_mapping	ni_name_mapping_t;
struct ni_name_mapping {
	ni_name_mapping_t *	next;
	char *			bus_name;
	char *			owner;
};

static ni_name_mapping_t *	ni_dbus_name_mappings;

static ni_name_mapping_t *
ni_name_mapping_new(const char *bus_name, const char *owner)
{
	ni_name_mapping_t *map;

	map = ni_malloc(sizeof(*map));
	ni_string_dup(&map->bus_name, bus_name);
	ni_string_dup(&map->owner, owner);
	return map;
}

static void
ni_name_mapping_free(ni_name_mapping_t *map)
{
	ni_string_free(&map->bus_name);
	ni_string_free(&map->owner);
	free(map);
}

void
ni_testbus_record_wellknown_bus_name(const char *bus_name, const char *owner)
{
	ni_name_mapping_t **pos, *map;

	if (owner && *owner == '\0')
		owner = NULL;

	for (pos = &ni_dbus_name_mappings; (map = *pos) != NULL; pos = &map->next) {
		if (ni_string_eq(map->bus_name, bus_name)) {
			if (owner == NULL) {
				/* Name released */
				*pos = map->next;
				ni_name_mapping_free(map);
			} else {
				/* Owner changed */
				ni_string_dup(&map->owner, owner);
			}
			return;
		}
	}

	if (owner)
		*pos = ni_name_mapping_new(bus_name, owner);
}

const char *
ni_testbus_lookup_wellknown_bus_name(const char *owner)
{
	ni_name_mapping_t *map;

	for (map = ni_dbus_name_mappings; map != NULL; map = map->next) {
		if (ni_string_eq(map->owner, owner))
			return map->bus_name;
	}

	return owner;
}

static void
handle_other_event(ni_event_t event)
{
	//ni_debug_events("%s(%s)", __func__, ni_event_type_to_name(event));
	if (dbus_server)
		ni_objectmodel_event_send_signal(dbus_server, event, NULL);
}
