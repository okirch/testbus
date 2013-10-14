
#ifndef __TESTBUS_SERVER_TEST_H__
#define __TESTBUS_SERVER_TEST_H__

#include "container.h"

struct ni_testbus_testcase {
	ni_testbus_testcase_t *	next;

	ni_testbus_container_t	context;
};

extern ni_testbus_testcase_t *	ni_testbus_testcase_by_name(ni_testbus_test_array_t *, const char *name);

extern ni_testbus_testcase_t *	ni_testbus_testcase_new(const char *name, ni_testbus_container_t *);
extern ni_testbus_testcase_t *	ni_testbus_testcase_cast(ni_testbus_container_t *);

extern void			ni_testbus_test_array_init(ni_testbus_test_array_t *test_array);
extern void			ni_testbus_test_array_destroy(ni_testbus_test_array_t *test_array);
extern void			ni_testbus_test_array_append(ni_testbus_test_array_t *, ni_testbus_testcase_t *);
extern ni_bool_t		ni_testbus_test_array_remove(ni_testbus_test_array_t *, const ni_testbus_testcase_t *);
extern ni_testbus_testcase_t *	ni_testbus_test_array_find_by_name(const ni_testbus_test_array_t *, const char *name);

static inline ni_testbus_testcase_t *
ni_testbus_testcase_get(ni_testbus_testcase_t *test)
{
	ni_testbus_container_get(&test->context);
	return test;
}

static inline void
ni_testbus_testcase_put(ni_testbus_testcase_t *test)
{
	ni_testbus_container_put(&test->context);
}

#endif /* __TESTBUS_SERVER_TEST_H__ */

