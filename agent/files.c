
#include <fcntl.h>
#include <dborb/process.h>
#include <dborb/buffer.h>
#include <testbus/client.h>

#include "files.h"

static ni_testbus_file_array_t	global_files;

ni_bool_t
ni_testbus_agent_process_attach_files(ni_process_t *pi, ni_testbus_file_array_t *files)
{
	unsigned int i;

	for (i = 0; i < files->count; ++i) {
		ni_testbus_file_t *file = files->data[i];
		ni_testbus_file_t *gfile;
		ni_dbus_object_t *file_object;

		gfile = ni_testbus_file_array_find_by_inum(&global_files, file->inum);
		if (gfile == NULL) {
			ni_testbus_file_array_append(&global_files, file);
		} else {
			/* Make sure the data we cached is still valid */
			if (gfile->iseq == file->iseq)
				continue;

			ni_testbus_file_drop_cache(gfile);

			ni_testbus_file_array_set(files, i, gfile);
			file = gfile;
		}

		/* Need to download file data */
		if (file->object_path == NULL) {
download_failed:
			ni_error("Cannot download file content for %s (object path %s)", file->name, file->object_path);
			return FALSE;
		}

		ni_debug_wicked("need to download file %s (inum %u)", file->name, file->inum);
		file_object = ni_testbus_call_get_and_refresh_object(file->object_path);
		if (!file_object)
			goto download_failed;
		file->data = ni_testbus_call_download_file(file_object);
		if (file->data == NULL)
			goto download_failed;

		file->size = ni_buffer_count(file->data);
		ni_debug_wicked("file %s (%s): downloaded %u bytes", file->name, file->object_path, file->size);
	}

	for (i = 0; i < files->count; ++i) {
		ni_tempstate_t *ts = ni_process_tempstate(pi);
		ni_testbus_file_t *file = files->data[i];
		const char *path;

		path = ni_tempstate_mkfile(ts, file->name, file->data);
		if (!path) {
			ni_error("unable to write file \"%s\"", file->name);
			return FALSE;
		}
		ni_string_dup(&file->instance_path, path);

		if (ni_string_eq(file->name, "stdin")) {
			/* attach to stdin */
			pi->stdin = open(file->instance_path, O_RDONLY);
			if (pi->stdin >= 0)
				ni_debug_wicked("process: attached file %s to stdin", file->name);
			else
				ni_warn("process: failed to attach file %s to stdin", file->name);
		}
	}

	return TRUE;
}
