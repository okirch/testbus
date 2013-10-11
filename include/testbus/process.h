
#ifndef __TESTBUS_PROCESS_H__
#define __TESTBUS_PROCESS_H__

#include <dborb/types.h>
#include <dborb/dbus.h>
#include <testbus/types.h>

extern ni_bool_t		ni_testbus_process_serialize(const ni_process_t *, ni_dbus_variant_t *);
extern ni_process_t *		ni_testbus_process_deserialize(const ni_dbus_variant_t *);
extern ni_bool_t		ni_testbus_process_exit_info_serialize(const ni_process_exit_info_t *, ni_dbus_variant_t *);
extern ni_process_exit_info_t *ni_testbus_process_exit_info_deserialize(const ni_dbus_variant_t *dict);

#endif /* __TESTBUS_PROCESS_H__ */
