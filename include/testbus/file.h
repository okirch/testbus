
#ifndef __TESTBUS_FILE_H__
#define __TESTBUS_FILE_H__

#include <dborb/types.h>
#include <dborb/dbus.h>
#include <testbus/types.h>


#define NI_TESTBUS_TMPFILE_SIZE_MAX	(1024 * 1024)

struct ni_testbus_file {
	unsigned int		refcount;

	unsigned int		id;

	char *			object_path;	/* master object path */
	char *			name;		/* file nickname (such as "stdin" or "hostfile") */
	unsigned int		inum;		/* globally unique "inode" */
	unsigned int		iseq;		/* sequence number of last change */

	char *			instance_path;	/* path name of file on disk, if needed */
	ni_buffer_t *		data;
	uint32_t		size;
	ni_bool_t		executable;
};

extern ni_bool_t		ni_testbus_file_serialize(const ni_testbus_file_t *, ni_dbus_variant_t *);
extern ni_testbus_file_t *	ni_testbus_file_deserialize(const ni_dbus_variant_t *, ni_testbus_file_array_t *);
extern ni_bool_t		ni_testbus_file_array_serialize(const ni_testbus_file_array_t *, ni_dbus_variant_t *);
extern ni_testbus_file_array_t *ni_testbus_file_array_deserialize(const ni_dbus_variant_t *);
extern void			ni_testbus_file_drop_cache(ni_testbus_file_t *);

extern void			ni_testbus_file_array_init(ni_testbus_file_array_t *);
extern void			ni_testbus_file_array_destroy(ni_testbus_file_array_t *);
extern void			ni_testbus_file_array_free(ni_testbus_file_array_t *);
extern void			ni_testbus_file_array_append(ni_testbus_file_array_t *, ni_testbus_file_t *);
extern ni_testbus_file_t *	ni_testbus_file_array_find_by_name(const ni_testbus_file_array_t *, const char *);
extern ni_testbus_file_t *	ni_testbus_file_array_find_by_inum(const ni_testbus_file_array_t *, unsigned int);
extern void			ni_testbus_file_array_set(ni_testbus_file_array_t *, unsigned int, ni_testbus_file_t *);
extern void			ni_testbus_file_array_merge(ni_testbus_file_array_t *result, const ni_testbus_file_array_t *);

extern ni_testbus_file_t *	ni_testbus_file_new(const char *, ni_testbus_file_array_t *);
extern ni_testbus_file_t *	ni_testbus_file_get(ni_testbus_file_t *);
extern void			ni_testbus_file_put(ni_testbus_file_t *);

#endif /* __TESTBUS_FILE_H__ */

