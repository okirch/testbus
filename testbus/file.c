
#include <stdlib.h>
#include <dborb/logging.h>
#include <dborb/util.h>
#include <dborb/buffer.h>

#include <testbus/file.h>

static void		ni_testbus_file_free(ni_testbus_file_t *file);

ni_testbus_file_t *
ni_testbus_file_new(const char *name, ni_testbus_file_array_t *file_array)
{
	static unsigned int __global_file_seq = 1;

	ni_testbus_file_t *file;

	file = ni_malloc(sizeof(*file));
	file->refcount = 1;
	file->id = __global_file_seq++;
	ni_string_dup(&file->name, name);

	ni_testbus_file_array_append(file_array, file);
	ni_testbus_file_put(file);

	return file;
}

ni_testbus_file_t *
ni_testbus_file_get(ni_testbus_file_t *file)
{
	ni_assert(file->refcount);
	file->refcount++;
	return file;
}

void
ni_testbus_file_put(ni_testbus_file_t *file)
{
	ni_assert(file->refcount);
	if (--(file->refcount) == 0)
		ni_testbus_file_free(file);
}

void
ni_testbus_file_free(ni_testbus_file_t *file)
{
	if (file->data)
		ni_buffer_free(file->data);
	ni_string_free(&file->instance_path);
	ni_string_free(&file->name);
	free(file);
}

void
ni_testbus_file_array_init(ni_testbus_file_array_t *file_array)
{
	memset(file_array, 0, sizeof(*file_array));
}

void
ni_testbus_file_array_destroy(ni_testbus_file_array_t *file_array)
{
	unsigned int i;

	for (i = 0; i < file_array->count; ++i)
		ni_testbus_file_put(file_array->data[i]);
	free(file_array->data);
	ni_testbus_file_array_init(file_array);
}

void
ni_testbus_file_array_append(ni_testbus_file_array_t *array, ni_testbus_file_t *file)
{
	array->data = ni_realloc(array->data, (array->count + 1) * sizeof(array->data[0]));
	array->data[array->count++] = ni_testbus_file_get(file);
}

#if 0
void
ni_testbus_file_array_remove(ni_testbus_file_array_t *fset, const ni_testbus_file_t *file)
{
	ni_testbus_file_t **pos, *f;

	for (pos = &fset->head; (f = *pos) != NULL; pos = &f->next) {
		if (f == file) {
			*pos = f->next;
			return;
		}
	}
}
#endif

extern ni_testbus_file_t *
ni_testbus_file_array_find_by_name(const ni_testbus_file_array_t *array, const char *name)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		ni_testbus_file_t *f = array->data[i];

		if (ni_string_eq(f->name, name))
			return f;
	}

	return NULL;
}

void
ni_testbus_file_array_merge(ni_testbus_file_array_t *result, const ni_testbus_file_array_t *merge)
{
	unsigned int i;

	for (i = 0; i < merge->count; ++i) {
		ni_testbus_file_t *f = merge->data[i];

		if (ni_testbus_file_array_find_by_name(result, f->name) != NULL)
			continue;
		ni_testbus_file_array_append(result, f);
	}
}
