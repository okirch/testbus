
#include <stdlib.h>
#include <dborb/util.h>
#include "testcase.h"

ni_testbus_testcase_t *
ni_testbus_testcase_new(const char *name, ni_testbus_container_t *parent)
{
	static unsigned int __global_testcase_seq = 1;

	ni_testbus_testcase_t *test;

	test = ni_malloc(sizeof(*test));
	test->id = __global_testcase_seq++;
	ni_string_dup(&test->name, name);

	ni_testbus_container_init(&test->context,
				NI_TESTBUS_CONTAINER_HAS_ENV |
				NI_TESTBUS_CONTAINER_HAS_CMDS |
				NI_TESTBUS_CONTAINER_HAS_FILES,
				parent);
	ni_testbus_testset_append(&parent->tests, test);

	return test;
}

void
ni_testbus_testcase_free(ni_testbus_testcase_t *test)
{
	ni_testbus_container_destroy(&test->context);
	ni_string_free(&test->name);
	free(test);
}

void
ni_testbus_testset_init(ni_testbus_testset_t *testset)
{
	memset(testset, 0, sizeof(*testset));
}

void
ni_testbus_testset_destroy(ni_testbus_testset_t *testset)
{
	ni_testbus_testcase_t *test;

	while ((test = testset->head) != NULL) {
		testset->head = test->next;
		ni_testbus_testcase_free(test);
	}
}

void
ni_testbus_testset_append(ni_testbus_testset_t *testset, ni_testbus_testcase_t *test)
{
	ni_testbus_testcase_t **pos, *f;

	for (pos = &testset->head; (f = *pos) != NULL; pos = &f->next)
		;
	*pos = test;
}

void
ni_testbus_testset_remove(ni_testbus_testset_t *testset, const ni_testbus_testcase_t *test)
{
	ni_testbus_testcase_t **pos, *f;

	for (pos = &testset->head; (f = *pos) != NULL; pos = &f->next) {
		if (f == test) {
			*pos = f->next;
			return;
		}
	}
}

ni_testbus_testcase_t *
ni_testbus_testset_find_by_name(const ni_testbus_testset_t *testset, const char *name)
{
	ni_testbus_testcase_t *f;

	for (f = testset->head; f != NULL; f = f->next) {
		if (ni_string_eq(f->name, name))
			return f;
	}

	return NULL;
}
