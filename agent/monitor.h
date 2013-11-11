
#ifndef __TESTBUS_MONITOR_AGENT_H__
#define __TESTBUS_MONITOR_AGENT_H__

#include <dborb/monitor.h>
#include <dborb/dbus.h>

extern void		ni_testbus_agent_eventlog_init(ni_dbus_object_t *);
extern ni_eventlog_t *	ni_testbus_agent_eventlog(void);
extern void		ni_testbus_agent_eventlog_flush(void);
extern void		ni_testbus_agent_register_monitor(ni_monitor_t *);
extern ni_bool_t	ni_testbus_agent_monitors_poll(void);

extern ni_monitor_t *	ni_agent_create_syslog_monitor(ni_eventlog_t *);

#endif /* __TESTBUS_MONITOR_AGENT_H__ */
