/*
 * Common DBus types and functions used for the implementation of a service
 *
 * Copyright (C) 2011-2014 Olaf Kirch <okir@suse.de>
 */


#ifndef __WICKED_DBUS_SERVICE_H__
#define __WICKED_DBUS_SERVICE_H__

#include <dborb/dbus.h>

extern ni_dbus_server_t *	ni_dbus_server_open(const char *bus_type, const char *bus_name, void *root_handle);
extern void			ni_dbus_server_free(ni_dbus_server_t *);
extern void			ni_dbus_service_bind_extension(const ni_dbus_service_t *service, const ni_extension_t *extension);
extern ni_dbus_property_t *	ni_dbus_service_register_property(const ni_dbus_service_t *service, const char *name, const char *binding);

typedef dbus_bool_t		ni_dbus_property_get_fn_t(const ni_dbus_object_t *,
					const ni_dbus_property_t *property,
					ni_dbus_variant_t *result,
					DBusError *error);
typedef dbus_bool_t		ni_dbus_property_set_fn_t(ni_dbus_object_t *,
					const ni_dbus_property_t *property,
					const ni_dbus_variant_t *value,
					DBusError *error);
typedef dbus_bool_t		ni_dbus_property_parse_fn_t(const ni_dbus_property_t *property,
					ni_dbus_variant_t *var,
					const char *value);
typedef void *			ni_dbus_property_get_handle_fn_t(const ni_dbus_object_t *object,
					ni_bool_t write_access,
					DBusError *error);

struct ni_dbus_property	{
	const char *			name;
	const char *			signature;

	struct {
		ni_dbus_property_get_handle_fn_t *get_handle;
		union {
			ni_bool_t *	bool_offset;
			int *		int_offset;
			unsigned int *	uint_offset;
			int16_t *	int16_offset;
			uint16_t *	uint16_offset;
			int64_t *	int64_offset;
			uint64_t *	uint64_offset;
			double *	double_offset;
			char **		string_offset;
			ni_string_array_t *string_array_offset;
			ni_uuid_t *	uuid_offset;
			const ni_dbus_property_t *dict_children;
		} u;
	} generic;

	ni_dbus_property_get_fn_t *	get;
	ni_dbus_property_set_fn_t *	set;
	ni_dbus_property_set_fn_t *	update;
	ni_dbus_property_parse_fn_t *	parse;
};

extern dbus_bool_t		ni_dbus_generic_property_get_bool(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_bool(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_bool(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_update_bool(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_get_int(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_int(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_int(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_uint(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_uint(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_uint(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_int16(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_int16(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_int16(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_uint16(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_uint16(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_uint16(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_int64(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_int64(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_int64(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_uint64(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_uint64(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_uint64(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_double(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_double(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_double(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_uuid(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_uuid(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_uuid(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_string(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_string(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_string(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);
extern dbus_bool_t		ni_dbus_generic_property_get_string_array(const ni_dbus_object_t *, const ni_dbus_property_t *,
					ni_dbus_variant_t *r, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_set_string_array(ni_dbus_object_t *, const ni_dbus_property_t *,
					const ni_dbus_variant_t *, DBusError *);
extern dbus_bool_t		ni_dbus_generic_property_parse_string_array(const ni_dbus_property_t *,
					ni_dbus_variant_t *, const char *);



#define __NI_DBUS_PROPERTY_RO(fstem, __name) \
	__NI_DBUS_PROPERTY_GET_FN(fstem, __name), \
	__NI_DBUS_PROPERTY_SET_FN(fstem, __name)
#define __NI_DBUS_PROPERTY_ROP(fstem, __name) \
	__NI_DBUS_PROPERTY_RO(fstem, __name), \
	__NI_DBUS_PROPERTY_PARSE_FN(fstem, __name)
#define __NI_DBUS_PROPERTY_RW(fstem, __name) \
	__NI_DBUS_PROPERTY_RO(fstem, __name), \
	__NI_DBUS_PROPERTY_UPDATE_FN(fstem, __name)
#define __NI_DBUS_PROPERTY_RWP(fstem, __name) \
	__NI_DBUS_PROPERTY_RW(fstem, __name), \
	__NI_DBUS_PROPERTY_PARSE_FN(fstem, __name)

#define __NI_DBUS_PROPERTY_GET_FN(fstem, __name) \
	.get = fstem ## _get_ ## __name
#define __NI_DBUS_PROPERTY_SET_FN(fstem, __name) \
	.set = fstem ## _set_ ## __name
#define __NI_DBUS_PROPERTY_UPDATE_FN(fstem, __name) \
	.update = fstem ## _update_ ## __name
#define __NI_DBUS_PROPERTY_PARSE_FN(fstem, __name) \
	.parse = fstem ## _parse_ ## __name

#define __NI_DBUS_DUMMY_PROPERTY(__signature, __name) { \
	.name = #__name, \
	.signature = __signature, \
}
#define NI_DBUS_DUMMY_PROPERTY(type, __name) \
	__NI_DBUS_DUMMY_PROPERTY(DBUS_TYPE_##type##_AS_STRING, __name)
#define __NI_DBUS_PROPERTY(__signature, __name, fstem, rw) \
	___NI_DBUS_PROPERTY(__signature, __name, __name, fstem, rw)
#define ___NI_DBUS_PROPERTY(__signature, __dbus_name, __member_name, fstem, rw) { \
	.name = #__dbus_name, \
	.signature = __signature, \
	__NI_DBUS_PROPERTY_##rw(fstem, __member_name), \
}
#define NI_DBUS_PROPERTY(type, __name, fstem, rw) \
	__NI_DBUS_PROPERTY(DBUS_TYPE_##type##_AS_STRING, __name, fstem, rw)

#define __NI_DBUS_GENERIC_PROPERTY(struct_name, dbus_sig, dbus_name, member_type, member_name, rw, args...) { \
	.name = #dbus_name, \
	.signature = dbus_sig, \
	__NI_DBUS_PROPERTY_##rw##P(ni_dbus_generic_property, member_type), \
	.generic = { \
		.get_handle = ni_objectmodel_get_##struct_name, \
		.u = { .member_type##_offset = &((ni_##struct_name##_t *) 0)->member_name }, \
	} \
	, ##args \
}
#define __NI_DBUS_GENERIC_DICT_PROPERTY(dbus_name, child_properties, rw) { \
	.name = #dbus_name, \
	.signature = NI_DBUS_DICT_SIGNATURE, \
	.generic = { \
		.u = { .dict_children = child_properties }, \
	} \
}
#define NI_DBUS_GENERIC_BOOL_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, DBUS_TYPE_BOOLEAN_AS_STRING, dbus_name, bool, member_name, rw)
#define NI_DBUS_GENERIC_INT_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, DBUS_TYPE_INT32_AS_STRING, dbus_name, int, member_name, rw)
#define NI_DBUS_GENERIC_UINT_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, DBUS_TYPE_UINT32_AS_STRING, dbus_name, uint, member_name, rw)
#define NI_DBUS_GENERIC_INT16_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, DBUS_TYPE_INT16_AS_STRING, dbus_name, int16, member_name, rw)
#define NI_DBUS_GENERIC_UINT16_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, DBUS_TYPE_UINT16_AS_STRING, dbus_name, uint16, member_name, rw)
#define NI_DBUS_GENERIC_INT32_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, DBUS_TYPE_INT32_AS_STRING, dbus_name, int, member_name, rw)
#define NI_DBUS_GENERIC_UINT32_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, DBUS_TYPE_UINT32_AS_STRING, dbus_name, uint, member_name, rw)
#define NI_DBUS_GENERIC_INT64_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, DBUS_TYPE_INT64_AS_STRING, dbus_name, int64, member_name, rw)
#define NI_DBUS_GENERIC_UINT64_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, DBUS_TYPE_UINT64_AS_STRING, dbus_name, uint64, member_name, rw)
#define NI_DBUS_GENERIC_DOUBLE_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, DBUS_TYPE_DOUBLE_AS_STRING, dbus_name, double, member_name, rw)
#define NI_DBUS_GENERIC_UUID_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, \
			NI_DBUS_BYTE_ARRAY_SIGNATURE, \
			dbus_name, uuid, member_name, rw)
#define NI_DBUS_GENERIC_STRING_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, DBUS_TYPE_STRING_AS_STRING, dbus_name, string, member_name, rw)
#define NI_DBUS_GENERIC_STRING_ARRAY_PROPERTY(struct_name, dbus_name, member_name, rw) \
	__NI_DBUS_GENERIC_PROPERTY(struct_name, \
			DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_STRING_AS_STRING, \
			dbus_name, string_array, member_name, rw)
#define NI_DBUS_GENERIC_DICT_PROPERTY(dbus_name, child_properties, rw) \
	__NI_DBUS_GENERIC_DICT_PROPERTY(dbus_name, child_properties, rw)



#endif /* __WICKED_DBUS_SERVICE_H__ */

