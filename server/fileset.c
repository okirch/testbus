
#include <stdlib.h>
#include <dborb/logging.h>
#include <dborb/util.h>
#include <dborb/buffer.h>
#include "fileset.h"

/*
 * Array of file sets.
 * Needed for the merging functionality
 */
void
ni_testbus_fileset_array_init(ni_testbus_fileset_array_t *array)
{
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_fileset_array_destroy(ni_testbus_fileset_array_t *array)
{
	free(array->data);
	memset(array, 0, sizeof(*array));
}

void
ni_testbus_fileset_array_append(ni_testbus_fileset_array_t *array, ni_testbus_fileset_t *env)
{
	array->data = ni_realloc(array->data, (array->count + 1) * sizeof(array->data[0]));
	array->data[array->count++] = env;
}

