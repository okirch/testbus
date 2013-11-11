

#include <sys/wait.h>

#include <dborb/monitor.h>
#include <dborb/dbus.h>
#include <dborb/buffer.h>
#include <testbus/monitor.h>

ni_bool_t
ni_testbus_event_serialize(const ni_event_t *ev, ni_dbus_variant_t *dict)
{
	uint64_t timestamp;

	ni_dbus_variant_init_dict(dict);
	ni_dbus_dict_add_string(dict, "source", ev->source);
	ni_dbus_dict_add_string(dict, "class", ev->class);
	ni_dbus_dict_add_string(dict, "type", ev->type);
	ni_dbus_dict_add_uint32(dict, "seq", ev->sequence);

	timestamp = 1000000 * ((uint64_t) ev->timestamp.tv_sec) + ev->timestamp.tv_usec;
	ni_dbus_dict_add_uint64(dict, "timestamp", timestamp);

	if (ev->data)
		ni_dbus_dict_add_byte_array_buffer(dict, "data", ev->data);

	return TRUE;
}

ni_bool_t
ni_testbus_event_deserialize(const ni_dbus_variant_t *dict, ni_event_t *ev)
{
	const char *class, *source, *type;
	uint64_t timestamp;
	uint32_t seq;

	memset(ev, 0, sizeof(*ev));
	if (!ni_dbus_dict_get_string(dict, "class", &class)
	 || !ni_dbus_dict_get_string(dict, "source", &source)
	 || !ni_dbus_dict_get_string(dict, "type", &type)
	 || !ni_dbus_dict_get_uint32(dict, "seq", &seq)
	 || !ni_dbus_dict_get_uint64(dict, "timestamp", &timestamp))
		return FALSE;

	ni_string_dup(&ev->class, class);
	ni_string_dup(&ev->source, source);
	ni_string_dup(&ev->type, type);
	ev->sequence = seq;

	ev->timestamp.tv_sec = timestamp / 1000000;
	ev->timestamp.tv_usec = timestamp % 1000000;

	ev->data = ni_dbus_dict_get_byte_array_buffer(dict, "data");

	return TRUE;
}
