/*
 * Global variables and config data
 *
 * Copyright (C) 2009-2013 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <signal.h>
#include <limits.h>

#include <dborb/netinfo.h>
#include "appconfig.h"

#define NI_DEFAULT_CONFIG_PATH	TESTBUS_CONFIGDIR "/config.xml"


/*
 * Global data for netinfo library
 */
ni_global_t	ni_global;
unsigned int	__ni_global_seqno;

/*
 * Global initialization of application
 */
int
ni_init(const char *appname)
{
	int explicit_config = 1;

	if (ni_global.initialized) {
		ni_error("ni_init called twice");
		return -1;
	}

	if (ni_global.config_path == NULL) {
		ni_string_dup(&ni_global.config_path, NI_DEFAULT_CONFIG_PATH);
		explicit_config = 0;
	}

	if (ni_file_exists(ni_global.config_path)) {
		ni_global.config = ni_config_parse(ni_global.config_path, NULL, NULL);
		if (!ni_global.config) {
			ni_error("Unable to parse netinfo configuration file");
			return -1;
		}
	} else {
		if (explicit_config) {
			ni_error("Configuration file %s does not exist",
					ni_global.config_path);
			return -1;
		}
		/* Create empty default configuration */
		ni_global.config = ni_config_new();
	}

	/* Our socket code relies on us ignoring this */
	signal(SIGPIPE, SIG_IGN);

	ni_global.initialized = 1;
	return 0;
}

void
ni_set_global_config_path(const char *pathname)
{
	ni_string_dup(&ni_global.config_path, pathname);
}

const char *
ni_config_piddir(void)
{
	ni_config_fslocation_t *fsloc = &ni_global.config->piddir;
	static ni_bool_t firsttime = TRUE;

	if (firsttime) {
		if (ni_mkdir_maybe(fsloc->path, fsloc->mode) < 0)
			ni_fatal("Cannot create pid file directory \"%s\": %m", fsloc->path);
		firsttime = FALSE;
	}

	return fsloc->path;
}

const char *
ni_config_statedir(void)
{
	ni_config_fslocation_t *fsloc = &ni_global.config->statedir;
	static ni_bool_t firsttime = TRUE;

	if (firsttime) {
		if (ni_mkdir_maybe(fsloc->path, fsloc->mode) < 0)
			ni_fatal("Cannot create state directory \"%s\": %m", fsloc->path);
		firsttime = FALSE;
	}

	return fsloc->path;
}

void
ni_server_listen_other_events(void (*event_handler)(ni_event_t))
{
	ni_global.other_event = event_handler;
}

/*
 * Utility functions for starting/stopping a daemon.
 */
ni_bool_t
ni_server_is_running(const char *appname)
{
	const char *piddir = ni_config_piddir();
	char pidfilepath[PATH_MAX];

	ni_assert(appname != NULL);
	snprintf(pidfilepath, sizeof(pidfilepath), "%s/%s.pid", piddir, appname);

	if (ni_pidfile_check(pidfilepath) > 0)
		return TRUE;

	return FALSE;
}

int
ni_server_background(const char *appname)
{
	const char *piddir = ni_config_piddir();
	char pidfilepath[PATH_MAX];

	ni_assert(appname != NULL);
	snprintf(pidfilepath, sizeof(pidfilepath), "%s/%s.pid", piddir, appname);
	return ni_daemonize(pidfilepath, 0644);
}

