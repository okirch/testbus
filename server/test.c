
#include <stdlib.h>
#include <dborb/util.h>
#include <dborb/logging.h>
#include "testcase.h"

static void		ni_testbus_testcase_destroy(ni_testbus_container_t *);
static void		ni_testbus_testcase_free(ni_testbus_container_t *);

static struct ni_testbus_container_ops ni_testbus_testcase_ops = {
	.features		= NI_TESTBUS_CONTAINER_HAS_ENV |
				  NI_TESTBUS_CONTAINER_HAS_CMDS |
				  NI_TESTBUS_CONTAINER_HAS_HOSTS |
				  NI_TESTBUS_CONTAINER_HAS_FILES,
	.dbus_name_prefix	= "Test",

	.destroy		= ni_testbus_testcase_destroy,
	.free			= ni_testbus_testcase_free,
};

ni_testbus_testcase_t *
ni_testbus_testcase_new(const char *name, ni_testbus_container_t *parent)
{
	ni_testbus_testcase_t *test;

	test = ni_malloc(sizeof(*test));

	ni_testbus_container_init(&test->context,
				&ni_testbus_testcase_ops,
				name,
				parent);

	return test;
}

ni_bool_t
ni_testbus_container_isa_testcase(const ni_testbus_container_t *container)
{
	return container->ops == &ni_testbus_testcase_ops;
}

ni_testbus_testcase_t *
ni_testbus_testcase_cast(ni_testbus_container_t *container)
{
	ni_testbus_testcase_t *test;

	ni_assert(container->ops == &ni_testbus_testcase_ops);
	test = ni_container_of(container, ni_testbus_testcase_t, context);
	return test;
}

void
ni_testbus_testcase_destroy(ni_testbus_container_t *container)
{
	ni_testbus_testcase_t *test = ni_testbus_testcase_cast(container);

}

void
ni_testbus_testcase_free(ni_testbus_container_t *container)
{
	ni_testbus_testcase_t *test = ni_testbus_testcase_cast(container);

	free(test);
}

void
ni_testbus_test_array_init(ni_testbus_test_array_t *array)
{
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_test_array_destroy(ni_testbus_test_array_t *array)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		ni_testbus_testcase_t *test = array->data[i];

		ni_testbus_testcase_put(test);
	}

	free(array->data);
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_test_array_append(ni_testbus_test_array_t *array, ni_testbus_testcase_t *test)
{
	array->data = ni_realloc(array->data, (array->count + 1) * sizeof(array->data[0]));
	array->data[array->count++] = ni_testbus_testcase_get(test);
}

int
ni_testbus_test_array_index(ni_testbus_test_array_t *array, const ni_testbus_testcase_t *test)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		if (array->data[i] == test)
			return i;
	}
	return -1;
}

ni_bool_t
ni_testbus_test_array_remove(ni_testbus_test_array_t *array, const ni_testbus_testcase_t *test)
{
	int index;

	if ((index = ni_testbus_test_array_index(array, test)) < 0)
		return FALSE;

	/* Drop the reference to the test */
	ni_testbus_testcase_put(array->data[index]);

	memmove(&array->data[index], &array->data[index+1], array->count - (index + 1));
	array->count --;
	return TRUE;
}

ni_testbus_testcase_t *
ni_testbus_test_array_find_by_name(const ni_testbus_test_array_t *array, const char *name)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		ni_testbus_testcase_t *test = array->data[i];

		if (ni_string_eq(test->context.name, name))
			return test;
	}
	return NULL;
}

