
#ifndef __TESTBUS_SERVER_TEST_H__
#define __TESTBUS_SERVER_TEST_H__

#include "container.h"

struct ni_testbus_testcase {
	ni_testbus_testcase_t *	next;

	char *			name;
	unsigned		id;

	ni_testbus_container_t	context;
};

extern ni_testbus_testcase_t *	ni_testbus_testcase_by_name(ni_testbus_testset_t *, const char *name);

extern ni_testbus_testcase_t *	ni_testbus_testcase_new(const char *name, ni_testbus_container_t *);
extern ni_testbus_testcase_t *	ni_testbus_testcase_get(ni_testbus_testcase_t *);
extern void			ni_testbus_testcase_put(ni_testbus_testcase_t *);
extern void			ni_testbus_testcase_free(ni_testbus_testcase_t *);

extern void			ni_testbus_testset_init(ni_testbus_testset_t *testset);
extern void			ni_testbus_testset_destroy(ni_testbus_testset_t *testset);
extern void			ni_testbus_testset_append(ni_testbus_testset_t *, ni_testbus_testcase_t *);
extern void			ni_testbus_testset_remove(ni_testbus_testset_t *, const ni_testbus_testcase_t *);
extern ni_testbus_testcase_t *	ni_testbus_testset_find_by_name(const ni_testbus_testset_t *, const char *name);

#endif /* __TESTBUS_SERVER_TEST_H__ */

