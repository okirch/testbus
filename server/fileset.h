
#ifndef __SERVER_FILESET_H__
#define __SERVER_FILESET_H__

#include "types.h"

#define NI_TESTBUS_FILESET_INIT		{ NULL }

typedef struct ni_testbus_fileset_array ni_testbus_fileset_array_t;
struct ni_testbus_fileset_array {
	unsigned int		count;
	ni_testbus_fileset_t **	data;
};

#define NI_TESTBUS_FILESET_ARRAY_INIT	{ .count = 0, .data = 0 }

extern void		ni_testbus_fileset_init(ni_testbus_fileset_t *fileset);
extern void		ni_testbus_fileset_destroy(ni_testbus_fileset_t *fileset);
extern void		ni_testbus_fileset_append(ni_testbus_fileset_t *, ni_testbus_tmpfile_t *);
extern void		ni_testbus_fileset_remove(ni_testbus_fileset_t *, const ni_testbus_tmpfile_t *);
extern ni_bool_t	ni_testbus_fileset_merge(ni_testbus_fileset_t *result, ni_testbus_fileset_array_t *);

extern void		ni_testbus_fileset_array_init(ni_testbus_fileset_array_t *);
extern void		ni_testbus_fileset_array_destroy(ni_testbus_fileset_array_t *);
extern void		ni_testbus_fileset_array_append(ni_testbus_fileset_array_t *, ni_testbus_fileset_t *);


#endif /* __SERVER_FILESET_H__ */
