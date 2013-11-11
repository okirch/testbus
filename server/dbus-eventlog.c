/*
 * Event log DBus interface
 *
 * The event log interface is always attached to a eventlog object.
 */

#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/logging.h>
#include <dborb/buffer.h>
#include <testbus/monitor.h>

#include "model.h"
#include "eventlog.h"
#include "eventlog.h"

/*
 * Send Eventlog.connected() signal
 */
static void
ni_testbus_eventlog_signal_eventsAdded(ni_dbus_object_t *eventlog_object, uint32_t last_seq)
{
	ni_dbus_variant_t arg = NI_DBUS_VARIANT_INIT;

	ni_dbus_variant_set_uint32(&arg, last_seq);

	/* Send the signal */
	ni_dbus_server_send_signal(ni_dbus_object_get_server(eventlog_object), eventlog_object,
			NI_TESTBUS_EVENTLOG_INTERFACE,
			"eventsAdded",
			1, &arg);

	ni_dbus_variant_destroy(&arg);
}

/*
 * Eventlog.add(event)
 */
static dbus_bool_t
__ni_Testbus_Eventlog_add(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_host_t *host;
	const ni_event_t *last;
	ni_event_t event, *ev;

	if (!(host = ni_testbus_host_unwrap(object, error)))
		return FALSE;

	if (argc != 1
	 || !ni_testbus_event_deserialize(&argv[0], &event))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if (host->eventlog == NULL)
		host->eventlog = ni_eventlog_new();

	last = ni_eventlog_last(host->eventlog);
	if (last && event.sequence != last->sequence + 1)
		ni_warn("%s: lost event(s): expected seq %u, got seq %u",
				object->path, last->sequence + 1, event.sequence);

	ni_debug_dbus("%s: adding %s-%s event from %s to log",
			host->context.name, event.class, event.type, event.source);

	/* This is a bit of a hack */
	ev = ni_event_array_add(&host->eventlog->events);
	*ev = event;

	ni_testbus_eventlog_signal_eventsAdded(object, event.sequence);
	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Eventlog, add);

void
ni_testbus_bind_builtin_eventlog(void)
{
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Eventlog_add_binding);
}
