
#ifndef __SERVER_FILESET_H__
#define __SERVER_FILESET_H__

#include "types.h"

#define NI_TESTBUS_TMPFILE_SIZE_MAX	(1024 * 1024)

struct ni_testbus_tmpfile {
	unsigned int		refcount;

	unsigned int		id;
	char *			name;
	char *			instance_path;
	ni_buffer_t *		data;
	uint32_t		size;
	ni_bool_t		executable;
};

#define NI_TESTBUS_FILESET_INIT		{ NULL }

typedef struct ni_testbus_fileset_array ni_testbus_fileset_array_t;
struct ni_testbus_fileset_array {
	unsigned int		count;
	ni_testbus_fileset_t **	data;
};

#define NI_TESTBUS_FILESET_ARRAY_INIT	{ .count = 0, .data = 0 }

ni_testbus_tmpfile_t *	ni_testbus_tmpfile_new(const char *, ni_testbus_file_array_t *);
ni_testbus_tmpfile_t *	ni_testbus_tmpfile_get(ni_testbus_tmpfile_t *);
extern void		ni_testbus_tmpfile_put(ni_testbus_tmpfile_t *);

extern void		ni_testbus_fileset_init(ni_testbus_fileset_t *fileset);
extern void		ni_testbus_fileset_destroy(ni_testbus_fileset_t *fileset);
extern void		ni_testbus_fileset_append(ni_testbus_fileset_t *, ni_testbus_tmpfile_t *);
extern void		ni_testbus_fileset_remove(ni_testbus_fileset_t *, const ni_testbus_tmpfile_t *);
extern ni_bool_t	ni_testbus_fileset_merge(ni_testbus_fileset_t *result, ni_testbus_fileset_array_t *);

extern void		ni_testbus_file_array_init(ni_testbus_file_array_t *);
extern void		ni_testbus_file_array_destroy(ni_testbus_file_array_t *);
extern void		ni_testbus_file_array_append(ni_testbus_file_array_t *, ni_testbus_tmpfile_t *);
ni_testbus_tmpfile_t *	ni_testbus_file_array_find_by_name(const ni_testbus_file_array_t *, const char *);
extern void		ni_testbus_file_array_merge(ni_testbus_file_array_t *result, const ni_testbus_file_array_t *);

extern void		ni_testbus_fileset_array_init(ni_testbus_fileset_array_t *);
extern void		ni_testbus_fileset_array_destroy(ni_testbus_fileset_array_t *);
extern void		ni_testbus_fileset_array_append(ni_testbus_fileset_array_t *, ni_testbus_fileset_t *);


#endif /* __SERVER_FILESET_H__ */
