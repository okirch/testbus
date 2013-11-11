
#ifndef __TESTBUS_MONITOR_H__
#define __TESTBUS_MONITOR_H__

#include <dborb/monitor.h>

extern ni_bool_t		ni_testbus_event_serialize(const ni_event_t *, ni_dbus_variant_t *);
extern ni_bool_t		ni_testbus_event_deserialize(const ni_dbus_variant_t *, ni_event_t *);

#endif /* __TESTBUS_MONITOR_H__ */
