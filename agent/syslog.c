/*
 * Agent syslog monitoring.
 *
 * Right now, all we do is set up a regular file monitor for /var/log/messages.
 * Instead, we could configure syslogd to forward all messages to us.
 * In a systemd world, there may be an even better way
 */

#include "monitor.h"

ni_monitor_t *
ni_agent_create_syslog_monitor(ni_eventlog_t *log)
{
	ni_monitor_t *mon;

	mon = ni_file_monitor_new("syslog", "/var/log/messages", log);
	mon->interval = 1;
	return mon;
}
