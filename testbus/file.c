
#include <stdlib.h>
#include <unistd.h>
#include <dborb/logging.h>
#include <dborb/util.h>
#include <dborb/buffer.h>

#include <testbus/file.h>

static void		ni_testbus_file_free(ni_testbus_file_t *file);

ni_testbus_file_t *
ni_testbus_file_new(const char *name, ni_testbus_file_array_t *file_array, unsigned int mode)
{
	static unsigned int __global_file_inum = 1;

	ni_testbus_file_t *file;

	file = ni_malloc(sizeof(*file));
	file->__magic = NI_TESTBUS_FILE_MAGIC;
	file->refcount = 1;
	file->inum = __global_file_inum++;
	file->id = file_array->next_id++;
	file->mode = mode? mode : NI_TESTBUS_FILE_READ;
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
	ni_testbus_file_check(file);
	ni_assert(file->refcount);
	if (--(file->refcount) == 0)
		ni_testbus_file_free(file);
}

void
ni_testbus_file_free(ni_testbus_file_t *file)
{
	ni_testbus_file_drop_cache(file);
	ni_string_free(&file->instance_path);
	ni_string_free(&file->name);
	ni_string_free(&file->object_path);
	file->__magic = 0xdeadbabe;
	free(file);
}

void
ni_testbus_file_drop_cache(ni_testbus_file_t *file)
{
	if (file->data)
		ni_buffer_free(file->data);
	file->data = NULL;

	if (file->instance_path) {
		unlink(file->instance_path);
		ni_string_free(&file->instance_path);
	}
}

ni_testbus_file_array_t *
ni_testbus_file_array_new(void)
{
	ni_testbus_file_array_t *file_array;

	file_array = ni_calloc(1, sizeof(*file_array));
	return file_array;
}

void
ni_testbus_file_array_free(ni_testbus_file_array_t *file_array)
{
	ni_testbus_file_array_destroy(file_array);
	free(file_array);
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

int
ni_testbus_file_array_index(ni_testbus_file_array_t *array, const ni_testbus_file_t *file)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		if (array->data[i] == file)
			return i;
	}
	return -1;
}

ni_bool_t
ni_testbus_file_array_remove(ni_testbus_file_array_t *array, const ni_testbus_file_t *file)
{
	int index;

	if ((index = ni_testbus_file_array_index(array, file)) < 0)
		return FALSE;

	/* Drop the reference to the file */
	ni_testbus_file_put(array->data[index]);

	memmove(&array->data[index], &array->data[index+1], (array->count - (index + 1)) * sizeof(array->data[0]));
	array->count --;
	return TRUE;
}

void
ni_testbus_file_array_set(ni_testbus_file_array_t *file_array, unsigned int index, ni_testbus_file_t *file)
{
	ni_assert(index < file_array->count);
	ni_testbus_file_get(file);
	ni_testbus_file_put(file_array->data[index]);
	file_array->data[index] = file;
}

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

ni_testbus_file_t *
ni_testbus_file_array_find_by_inum(const ni_testbus_file_array_t *array, unsigned int inum)
{
	unsigned int i;

	for (i = 0; i < array->count; ++i) {
		ni_testbus_file_t *f = array->data[i];

		if (f->inum == inum)
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

/*
 * Serialize/deserialize a file array
 * This is used in the processScheduled() signal sent from master to agent.
 * We do not transmit the file content, but only the metadata.
 * The agent can then decide whether he needs to retrieve the data or not.
 */
ni_bool_t
ni_testbus_file_serialize(const ni_testbus_file_t *file, ni_dbus_variant_t *dict)
{
	ni_dbus_variant_init_dict(dict);
	ni_dbus_dict_add_string(dict, "name", file->name);
	ni_dbus_dict_add_uint32(dict, "inum", file->inum);
	ni_dbus_dict_add_uint32(dict, "iseq", file->iseq);
	ni_dbus_dict_add_uint32(dict, "mode", file->mode);
	if (file->object_path)
		ni_dbus_dict_add_string(dict, "object-path", file->object_path);
	return TRUE;
}

ni_testbus_file_t *
ni_testbus_file_deserialize(const ni_dbus_variant_t *dict, ni_testbus_file_array_t *container)
{
	ni_testbus_file_t *file;
	const char *name, *path;
	uint32_t inum, iseq, mode;

	if (!ni_dbus_dict_get_string(dict, "name", &name))
		return NULL;

	if (!ni_dbus_dict_get_uint32(dict, "inum", &inum)
	 || !ni_dbus_dict_get_uint32(dict, "iseq", &iseq)
	 || !ni_dbus_dict_get_uint32(dict, "mode", &mode))
		return FALSE;

	file = ni_testbus_file_new(name, container, mode);
	file->inum = inum;
	file->iseq = iseq;

	if (ni_dbus_dict_get_string(dict, "object-path", &path)) {
		ni_string_dup(&file->object_path, path);
	}

	return file;
}

ni_bool_t
ni_testbus_file_array_serialize(const ni_testbus_file_array_t *file_array, ni_dbus_variant_t *dict_array)
{
	unsigned int i;

	ni_dbus_dict_array_init(dict_array);
	for (i = 0; i < file_array->count; ++i) {
		ni_dbus_variant_t *e = ni_dbus_dict_array_add(dict_array);

		if (!ni_testbus_file_serialize(file_array->data[i], e))
			return FALSE;
	}

	return TRUE;
}

ni_testbus_file_array_t *
ni_testbus_file_array_deserialize(const ni_dbus_variant_t *dict_array)
{
	ni_testbus_file_array_t *file_array;
	unsigned int i;

	file_array = ni_testbus_file_array_new();
	for (i = 0; i < dict_array->array.len; ++i) {
		const ni_dbus_variant_t *e;
		ni_testbus_file_t *file;

		if (!(e = ni_dbus_dict_array_at(dict_array, i)))
			goto failed;
		file = ni_testbus_file_deserialize(e, file_array);
		if (file == NULL)
			goto failed;
	}

	return file_array;

failed:
	ni_testbus_file_array_free(file_array);
	return NULL;
}
