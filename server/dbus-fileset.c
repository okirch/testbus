
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/logging.h>
#include <dborb/buffer.h>

#include <testbus/file.h>

#include "fileset.h"
#include "model.h"
#include "container.h"

void
ni_testbus_create_static_objects_file(ni_dbus_server_t *server)
{
}

const char *
ni_testbus_file_full_path(const ni_dbus_object_t *container_object, const ni_testbus_file_t *file)
{
	static char pathbuf[256];

	snprintf(pathbuf, sizeof(pathbuf), "%s/File%u", container_object->path, file->id);
	return pathbuf;
}

ni_dbus_object_t *
ni_testbus_file_wrap(ni_dbus_object_t *container_object, ni_testbus_file_t *file)
{
	ni_dbus_object_t *file_object;

	file_object = ni_objectmodel_create_object(
			ni_dbus_object_get_server(container_object),
			ni_testbus_file_full_path(container_object, file),
			ni_testbus_file_class(),
			ni_testbus_file_get(file));

	/* This is a bit of a layering violation, but we need this piece of information
	 * in the processScheduled signal */
	ni_string_dup(&file->object_path, file_object->path);
	return file_object;
}

ni_testbus_file_t *
ni_testbus_file_unwrap(const ni_dbus_object_t *object, DBusError *error)
{
	ni_testbus_file_t *file;

	file = ni_dbus_object_get_handle_typecheck(object, ni_testbus_file_class(), error);
	return file;
}

static void
__ni_testbus_file_unregister(ni_dbus_object_t *object, ni_testbus_file_t *file)
{
	if (object) {
		ni_dbus_server_send_signal(ni_dbus_object_get_server(object), object,
				NI_TESTBUS_TMPFILE_INTERFACE,
				"deleted",
				0, NULL);
	}

	ni_testbus_file_check(file);
	ni_string_free(&file->object_path);
}

void
ni_testbus_file_unregister(ni_testbus_file_t *file)
{
	if (file->object_path) {
		ni_dbus_object_t *object;

		object = ni_objectmodel_object_by_path(file->object_path);
		if (object)
			ni_dbus_server_unregister_object(object);
	}
}

static void
__ni_testbus_file_object_destroy(ni_dbus_object_t *object)
{
	ni_testbus_file_t *file;

	ni_debug_testbus("%s(%s)", __func__, object->path);
	if ((file = ni_testbus_file_unwrap(object, NULL)) != NULL) {
		ni_testbus_file_check(file);
		__ni_testbus_file_unregister(object, file);
		ni_testbus_file_put(file);
		object->handle = NULL;
	}
}

void *
ni_objectmodel_get_testbus_file(const ni_dbus_object_t *object, ni_bool_t write_access, DBusError *error)
{
	return ni_testbus_file_unwrap(object, error);
}

/*
 * Fileset.createFile(name, optional attrdict)
 *
 */
static dbus_bool_t
__ni_Testbus_Fileset_createFile(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_container_t *context;
	ni_dbus_object_t *file_object;
	ni_testbus_file_t *file;
	const char *name;
	uint32_t mode = 0;

	if ((context = ni_testbus_container_unwrap(object, error)) == NULL)
		return FALSE;

	if (argc == 0
	 || !ni_dbus_variant_get_string(&argv[0], &name)
	 || (argc == 2 && !ni_dbus_variant_get_uint32(&argv[1], &mode))
	 || argc > 2)
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if (!ni_testbus_identifier_valid(name, error))
		return FALSE;

	if (__ni_testbus_container_get_file_by_name(context, name) != NULL) {
		dbus_set_error(error, NI_DBUS_ERROR_NAME_EXISTS, "tmpfile with this name already exists");
		return FALSE;
	}

	ni_debug_testbus("%s: creating file \"%s\"", object->path, name);
	if ((file = ni_testbus_file_new(name, &context->files, mode)) == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "unable to create new file \"%s\"", name);
		return FALSE;
	}


	/* Register this object */
	file_object = ni_testbus_file_wrap(object, file);
	ni_dbus_message_append_string(reply, file_object->path);

	return TRUE;
}

static NI_TESTBUS_METHOD_BINDING(Fileset, createFile);

/*
 * Tmpfile.append(data)
 */
static dbus_bool_t
__ni_Testbus_Tmpfile_append(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_testbus_file_t *file;
	unsigned int count;

	if ((file = ni_testbus_file_unwrap(object, error)) == NULL)
		return FALSE;

	if (argc != 1
	 || !ni_dbus_variant_is_byte_array(&argv[0]))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	count = argv[0].array.len;
	if (file->data == NULL)
		file->data = ni_buffer_new(count < 4096? count : 0);

	if (count > NI_TESTBUS_TMPFILE_SIZE_MAX
	 || count + ni_buffer_count(file->data) > NI_TESTBUS_TMPFILE_SIZE_MAX) {
		dbus_set_error(error, NI_DBUS_ERROR_BAD_SIZE, "file too big");
		return FALSE;
	}

	if (!ni_buffer_ensure_tailroom(file->data, count)) {
		dbus_set_error(error, NI_DBUS_ERROR_BAD_SIZE, "file too big");
		return FALSE;
	}

	ni_buffer_put(file->data, argv[0].byte_array_value, count);
	file->size = ni_buffer_count(file->data);
	file->iseq++;

	ni_debug_testbus("file %s: appended %u bytes (now %u bytes total)",
			file->name, count, ni_buffer_count(file->data));
	return TRUE;
}

static NI_TESTBUS_METHOD_BINDING(Tmpfile, append);

/*
 * Tmpfile.retrieve(data)
 */
static dbus_bool_t
__ni_Testbus_Tmpfile_retrieve(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	ni_testbus_file_t *file;
	uint64_t offset;
	uint32_t count;
	ni_bool_t rv;

	if ((file = ni_testbus_file_unwrap(object, error)) == NULL)
		return FALSE;

	if (argc != 2
	 || !ni_dbus_variant_get_uint64(&argv[0], &offset)
	 || !ni_dbus_variant_get_uint32(&argv[1], &count)
	 || count > 65536)
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	ni_dbus_variant_init_byte_array(&res);
	if (file->data == NULL) {
		ni_debug_testbus("%s: no data", file->name);
	} else {
		uint64_t size = ni_buffer_count(file->data);
		unsigned char *data;

		if (offset < size) {
			data = ni_buffer_head(file->data) + offset;
			if (size - offset < count)
				count = size - offset;
			ni_dbus_variant_set_byte_array(&res, data, count);
		}
	}

	rv = ni_dbus_message_serialize_variants(reply, 1, &res, error);
	if (rv)
		ni_debug_testbus("file %s: retrieved %u bytes", file->name, res.array.len);
	ni_dbus_variant_destroy(&res);

	return rv;
}

static NI_TESTBUS_METHOD_BINDING(Tmpfile, retrieve);

static ni_dbus_property_t       __ni_Testbus_Tmpfile_properties[] = {
	NI_DBUS_GENERIC_STRING_PROPERTY(testbus_file, name, name, RO),
	NI_DBUS_GENERIC_UINT32_PROPERTY(testbus_file, size, size, RO),
	{ NULL }
};
NI_TESTBUS_PROPERTIES_BINDING(Tmpfile);


void
ni_testbus_bind_builtin_file(void)
{
	const ni_dbus_class_t *class;

	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Fileset_createFile_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Tmpfile_append_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Tmpfile_retrieve_binding);
	ni_dbus_objectmodel_bind_properties(&__ni_Testbus_Tmpfile_Properties_binding);

	class = ni_testbus_file_class();
	((ni_dbus_class_t *) class)->destroy = __ni_testbus_file_object_destroy;
}

