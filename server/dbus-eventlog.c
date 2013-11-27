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
#include "host.h"

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

static ni_eventlog_t *
__ni_objectmodel_get_eventlog(const ni_dbus_object_t *object, ni_bool_t write_access, DBusError *error)
{
	ni_testbus_host_t *host;

	if (!(host = ni_testbus_host_unwrap(object, error)))
		return FALSE;

	if (host->eventlog == NULL) {
		if (!write_access) {
			dbus_set_error(error, NI_DBUS_ERROR_PROPERTY_NOT_PRESENT,
					"%s has no eventlog", object->path);
		        return FALSE;
		}

		host->eventlog = ni_eventlog_new();
	}

	return host->eventlog;
}

static void *
ni_objectmodel_get_eventlog(const ni_dbus_object_t *object, ni_bool_t write_access, DBusError *error)
{
	return __ni_objectmodel_get_eventlog(object, write_access, error);
}

/*
 * Eventlog.add(event)
 */
static dbus_bool_t
__ni_Testbus_Eventlog_add(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_eventlog_t *log;
	const ni_event_t *last;
	ni_event_t *ev;
	uint32_t last_seq = 0;

	if (!(log = __ni_objectmodel_get_eventlog(object, TRUE, error)))
		return FALSE;

	if ((last = ni_eventlog_last(log)) != NULL)
		last_seq = last->sequence;

	if (argc != 1)
		goto invalid_args;

	ev = ni_event_array_add(&log->events);
	if (!ni_testbus_event_deserialize(&argv[0], ev)) {
		/* Dispose of the incomplete event. Would be nice if there were
		 * a proper function for this. */
		ni_event_destroy(ev);
		log->events.count -= 1;

		goto invalid_args;
	}

	if (last_seq && ev->sequence != last_seq + 1)
		ni_warn("%s: lost event(s): expected seq %u, got seq %u",
				object->path, last_seq + 1, ev->sequence);

	ni_debug_dbus("%s: adding %s-%s event from %s to log",
			object->path, ev->class, ev->type, ev->source);

	ni_testbus_eventlog_signal_eventsAdded(object, ev->sequence);
	return TRUE;

invalid_args:
	return ni_dbus_error_invalid_args(error, object->path, method->name);
}

NI_TESTBUS_METHOD_BINDING(Eventlog, add);

/*
 * Eventlog.purge(seqno)
 */
static dbus_bool_t
__ni_Testbus_Eventlog_purge(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_eventlog_t *log;
	uint32_t upto_seq;

	if (!(log = __ni_objectmodel_get_eventlog(object, TRUE, error)))
		return FALSE;

	if (argc != 1
	 || !ni_dbus_variant_get_uint32(&argv[0], &upto_seq))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if (upto_seq == 0) {
		/* Means: really flush all events */
		ni_eventlog_flush(log);
	} else {
		/* Mark all events up to and including upto_seq as consumed.
		 */
		ni_eventlog_consume_upto(log, upto_seq);
	}

	return TRUE;
}

NI_TESTBUS_METHOD_BINDING(Eventlog, purge);

/*
  <define name="properties" class="dict">
    <last-seq type="uint32" />
    <events class="array" element-type="event_t" />
  </define>
 */
static dbus_bool_t
__ni_testbus_eventlog_get_events(const ni_dbus_object_t *object, const ni_dbus_property_t *property,
			ni_dbus_variant_t *result, DBusError *error)
{
	ni_eventlog_t *log;
	unsigned int i;

	if (!(log = __ni_objectmodel_get_eventlog(object, FALSE, error)))
		return FALSE;

	ni_dbus_dict_array_init(result);
	for (i = log->consumed; i < log->events.count; ++i) {
		ni_event_t *ev = &log->events.data[i];

		ni_testbus_event_serialize(ev, ni_dbus_dict_array_add(result));
	}
	return TRUE;
}

static dbus_bool_t
__ni_testbus_eventlog_set_events(ni_dbus_object_t *object, const ni_dbus_property_t *property,
			const ni_dbus_variant_t *value, DBusError *error)
{
	return FALSE;
}

static ni_dbus_property_t       __ni_Testbus_Eventlog_properties[] = {
	NI_DBUS_GENERIC_UINT32_PROPERTY(eventlog, last-seq, seqno, RO),
	__NI_DBUS_PROPERTY(NI_DBUS_DICT_ARRAY_SIGNATURE, events, __ni_testbus_eventlog, RO),
	{ NULL }
};
NI_TESTBUS_PROPERTIES_BINDING(Eventlog);

void
ni_testbus_bind_builtin_eventlog(void)
{
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Eventlog_add_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Eventlog_purge_binding);
	ni_dbus_objectmodel_bind_properties(&__ni_Testbus_Eventlog_Properties_binding);
}
