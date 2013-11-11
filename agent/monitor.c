
#include <dborb/socket.h>
#include <testbus/client.h>
#include "monitor.h"

static ni_monitor_array_t	__ni_monitors;
static ni_eventlog_t *		__ni_agent_eventlog;
static ni_dbus_object_t *	__ni_eventlog_object;
static const ni_timer_t *	__ni_mon_timer;
static unsigned long		__ni_mon_timeout;

static void			__ni_testbus_agent_monitors_poll_timeout(void *, const ni_timer_t *);

void
ni_testbus_agent_eventlog_init(ni_dbus_object_t *object)
{
	ni_assert(__ni_agent_eventlog == NULL);
	__ni_eventlog_object = object;
	__ni_agent_eventlog = ni_eventlog_new();
}

ni_eventlog_t *
ni_testbus_agent_eventlog(void)
{
	ni_assert(__ni_agent_eventlog != NULL);
	return __ni_agent_eventlog;
}

/*
 * Flush the event log to the server
 */
void
ni_testbus_agent_eventlog_flush(void)
{
	ni_eventlog_t *log = __ni_agent_eventlog;
	const ni_event_t *ev;

	if (log == NULL || ni_eventlog_pending_count(log) == 0)
		return;

	ni_debug_testbus("pushing %u events to master", ni_eventlog_pending_count(log));

	ni_assert(__ni_eventlog_object);
	while ((ev = ni_eventlog_consume(log)) != NULL)
		ni_testbus_client_eventlog_append(__ni_eventlog_object, ev);

	ni_eventlog_prune(log);
}

void
ni_testbus_agent_register_monitor(ni_monitor_t *mon)
{
	unsigned int i, interval = 0;

	ni_monitor_array_append(&__ni_monitors, mon);
	for (i = 0; i < __ni_monitors.count; ++i) {
		ni_monitor_t *mon = __ni_monitors.data[i];

		if (mon->interval) {
			if (!interval || mon->interval < interval)
				interval = mon->interval;
		}
	}

	__ni_mon_timeout = interval * 1000;
	if (__ni_mon_timeout) {
		if (__ni_mon_timer)
			__ni_mon_timer = ni_timer_rearm(__ni_mon_timer, __ni_mon_timeout);
		else
			__ni_mon_timer = ni_timer_register(__ni_mon_timeout, __ni_testbus_agent_monitors_poll_timeout, NULL);
	}
}

ni_bool_t
ni_testbus_agent_monitors_poll(void)
{
	unsigned int i;
	ni_bool_t rv;

	for (i = 0; i < __ni_monitors.count; ++i) {
		ni_monitor_t *mon = __ni_monitors.data[i];

		if (ni_monitor_poll(mon) && mon->push)
			rv = TRUE;
	}

	return rv;
}

static void
__ni_testbus_agent_monitors_poll_timeout(void *user_data, const ni_timer_t *timer)
{
#ifdef notworkie
	__ni_mon_timer = ni_timer_rearm(timer, __ni_mon_timeout);
#else
	__ni_mon_timer = ni_timer_register(__ni_mon_timeout, __ni_testbus_agent_monitors_poll_timeout, NULL);
#endif

	if (ni_testbus_agent_monitors_poll())
		ni_testbus_agent_eventlog_flush();
}
