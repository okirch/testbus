
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dborb/dbus-errors.h>
#include <dborb/dbus-service.h>
#include <dborb/logging.h>
#include <dborb/buffer.h>
#include <testbus/model.h>

#include "dbus-filesystem.h"

void
ni_testbus_create_static_objects_filesystem(ni_dbus_server_t *server)
{
	ni_objectmodel_create_object(server, NI_TESTBUS_AGENT_FS_PATH, ni_testbus_filesystem_class(), NULL);
}

/*
 * Filesystem.getInfo(path)
 *
 */
static dbus_bool_t
__ni_Testbus_Agent_Filesystem_getInfo(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	struct stat stb;
	const char *path;
	dbus_bool_t rv;

	if (argc != 1 || !ni_dbus_variant_get_string(&argv[0], &path) || path[0] != '/')
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if (stat(path, &stb) < 0) {
		ni_dbus_set_error_from_errno(error, errno, "unable to stat file \"%s\"", path);
		return FALSE;
	}
	if (!S_ISREG(stb.st_mode)) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "not a regular file");
		return FALSE;
	}

	ni_dbus_variant_init_dict(&res);
	ni_dbus_dict_add_uint64(&res, "size", stb.st_size);

	rv = ni_dbus_message_serialize_variants(reply, 1, &res, error);
	ni_dbus_variant_destroy(&res);
	return rv;
}

__NI_TESTBUS_METHOD_BINDING(Agent_Filesystem, getInfo, NI_TESTBUS_NAMESPACE ".Agent.Filesystem");

/*
 * Filesystem.download(path, offset, count)
 *
 */
static dbus_bool_t
__ni_Testbus_Agent_Filesystem_download(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_dbus_variant_t res = NI_DBUS_VARIANT_INIT;
	const char *path;
	uint64_t offset;
	uint32_t count;
	dbus_bool_t rv;
	ni_buffer_t *bp = NULL;
	int fd = -1;

	if (argc != 3
	 || !ni_dbus_variant_get_string(&argv[0], &path) || path[0] != '/'
	 || !ni_dbus_variant_get_uint64(&argv[1], &offset)
	 || !ni_dbus_variant_get_uint32(&argv[2], &count)
	 || count > 1024 * 1024
	 || offset + count < offset)
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if ((fd = open(path, O_RDONLY)) < 0) {
		ni_dbus_set_error_from_errno(error, errno, "unable to open file \"%s\"", path);
		return FALSE;
	}
	if (lseek(fd, offset, SEEK_SET) < 0) {
		ni_dbus_set_error_from_errno(error, errno, "seek faile");
		goto out_fail;
	}

	bp = ni_buffer_new(count);
	while (count) {
		int n;

		n = read(fd, ni_buffer_tail(bp), ni_buffer_tailroom(bp));
		if (n < 0) {
			ni_dbus_set_error_from_errno(error, errno, "read failed");
			goto out_fail;
		}
		if (n == 0)
			break;
		ni_buffer_push_tail(bp, n);
	}

	ni_dbus_variant_init_dict(&res);
	ni_dbus_variant_set_byte_array(&res, ni_buffer_head(bp), ni_buffer_count(bp));

	rv = ni_dbus_message_serialize_variants(reply, 1, &res, error);

	ni_dbus_variant_destroy(&res);
	ni_buffer_free(bp);
	close(fd);
	return rv;

out_fail:
	if (fd >= 0)
		close(fd);
	return FALSE;
}

__NI_TESTBUS_METHOD_BINDING(Agent_Filesystem, download, NI_TESTBUS_NAMESPACE ".Agent.Filesystem");

/*
 * Tmpfile.upload(path, offset, data)
 */
static dbus_bool_t
__ni_Testbus_Agent_Filesystem_upload(ni_dbus_object_t *object, const ni_dbus_method_t *method,
		unsigned int argc, const ni_dbus_variant_t *argv,
		ni_dbus_message_t *reply, DBusError *error)
{
	ni_buffer_t wbuf;
	const char *path;
	uint64_t offset;
	unsigned int written = 0;
	int fd;

	if (argc != 3
	 || !ni_dbus_variant_get_string(&argv[0], &path) || path[0] != '/'
	 || !ni_dbus_variant_get_uint64(&argv[1], &offset)
	 || !ni_dbus_variant_is_byte_array(&argv[2]))
		return ni_dbus_error_invalid_args(error, object->path, method->name);

	if (offset == 0)
		fd = open(path, O_CREAT|O_TRUNC|O_WRONLY, 0644);
	else
		fd = open(path, O_WRONLY);
	if (fd < 0) {
		ni_dbus_set_error_from_errno(error, errno, "unable to open file \"%s\"", path);
		return FALSE;
	}
	if (lseek(fd, offset, SEEK_SET) < 0) {
		ni_dbus_set_error_from_errno(error, errno, "seek faile");
		goto out_fail;
	}

	ni_buffer_init(&wbuf, argv[2].byte_array_value, argv[2].array.len);
	while (ni_buffer_count(&wbuf)) {
		int n;

		n = write(fd, ni_buffer_head(&wbuf), ni_buffer_count(&wbuf));
		if (n < 0) {
			ni_dbus_set_error_from_errno(error, errno,
						"error writing to \"%s\" at offset %Lu",
						path, (unsigned long long) offset + written);
			goto out_fail;
		}

		ni_buffer_pull_head(&wbuf, n);
		written += n;
	}

	close(fd);

	ni_debug_wicked("%s: wrote %u bytes at offset %Lu",
			path, written, (unsigned long long) offset);
	return TRUE;

out_fail:
	if (fd >= 0)
		close(fd);
	return FALSE;
}

__NI_TESTBUS_METHOD_BINDING(Agent_Filesystem, upload, NI_TESTBUS_NAMESPACE ".Agent.Filesystem");

void
ni_testbus_bind_builtin_filesystem(void)
{
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Agent_Filesystem_getInfo_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Agent_Filesystem_download_binding);
	ni_dbus_objectmodel_bind_method(&__ni_Testbus_Agent_Filesystem_upload_binding);
}
