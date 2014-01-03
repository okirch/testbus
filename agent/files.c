
#include <fcntl.h>
#include <sys/stat.h>
#include <dborb/process.h>
#include <dborb/buffer.h>
#include <testbus/client.h>

#include "files.h"

static ni_testbus_file_array_t	global_files;

static ni_bool_t		ni_testbus_agent_download_file(ni_testbus_file_t *);

ni_bool_t
ni_testbus_agent_process_attach_files(ni_process_t *pi, ni_testbus_file_array_t *files)
{
	unsigned int i;

	for (i = 0; i < files->count; ++i) {
		ni_testbus_file_t *file = files->data[i];
		ni_testbus_file_t *gfile;

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

		/* only download files marked as NI_TESTBUS_FILE_READ */
		if (file->mode & NI_TESTBUS_FILE_READ) {
			if (!ni_testbus_agent_download_file(file)) {
				ni_error("Cannot download file content for %s (object path %s)", file->name, file->object_path);
				return FALSE;
			}
		}
	}

	pi->stdout.capture = TRUE;

	for (i = 0; i < files->count; ++i) {
		ni_tempstate_t *ts = ni_process_tempstate(pi);
		ni_testbus_file_t *file = files->data[i];
		const char *path;

		/* There's no need to store stderr/stdout in real tempfiles */
		if (ni_string_eq(file->name, "stdout")) {
			/* Nothing */
			continue;
		}
		if (ni_string_eq(file->name, "stderr")) {
			pi->stderr.capture = TRUE;
			continue;
		}

		path = ni_tempstate_mkfile(ts, file->name, file->data);
		if (!path) {
			ni_error("unable to write file \"%s\"", file->name);
			return FALSE;
		}
		ni_string_dup(&file->instance_path, path);

		if (file->mode & NI_TESTBUS_FILE_EXEC) {
			chmod(file->instance_path, 0755);
		}

		/* FIXME: record the file's md5 hash, so that we
		 * can detect whether the file changed during the process run.
		 * FIXME2: should we mark files as read, write, readwrite?
		 */

		if (ni_string_eq(file->name, "stdin")) {
			/* attach to stdin */
			if (ni_process_attach_input_path(pi, file->instance_path))
				ni_debug_testbus("process: attached file %s to stdin", file->name);
			else
				ni_warn("process: failed to attach file %s to stdin", file->name);
		}
	}

	return TRUE;
}

/*
 * Download a file
 */
ni_bool_t
ni_testbus_agent_download_file(ni_testbus_file_t *file)
{
	ni_dbus_object_t *file_object;

	/* Need to download file data */
	if (file->object_path == NULL)
		return FALSE;

	ni_debug_testbus("need to download file %s (inum %u)", file->name, file->inum);
	file_object = ni_testbus_client_get_and_refresh_object(file->object_path);
	if (!file_object)
		return FALSE;
		
	file->data = ni_testbus_client_download_file(file_object);
	if (file->data == NULL)
		return FALSE;

	file->size = ni_buffer_count(file->data);
	ni_debug_testbus("file %s (%s): downloaded %u bytes",
			file->name, file->object_path, file->size);
	return TRUE;
}

/*
 * Substitute %{...} strings in arguments and file names
 */
static ni_bool_t
ni_testbus_agent_substitute(char **sp, const ni_string_array_t *env, const ni_testbus_file_array_t *files)
{
	ni_stringbuf_t buf = NI_STRINGBUF_INIT_DYNAMIC;
	char *s = *sp, *pos;

	if (strchr(s, '%') == NULL)
		return TRUE;

	for (pos = *sp; *pos; ) {
		char *var_start;
		char cc = *pos++;

		if (cc != '%') {
			ni_stringbuf_putc(&buf, cc);
			continue;
		}

		cc = *pos++;
		if (cc == '%') {
			ni_stringbuf_putc(&buf, cc);
			continue;
		}
		if (cc == '\0') {
			ni_error("expansion error: %% at end of string");
			goto failed;
		}
		if (cc != '{') {
			ni_error("expansion error: %% followed by %c", cc);
			goto failed;
		}

		var_start = pos;
		while ((cc = *pos++) != '}') {
			if (cc == '\0') {
				ni_error("expansion error: \"%%{%s\" lacks closing bracket", var_start);
				goto failed;
			}
		}

		pos[-1] = '\0';

		if (!strncmp(var_start, "file:", 5)) {
			char *name = var_start + 5;
			ni_testbus_file_t *file;

			file = ni_testbus_file_array_find_by_name(files, name);
			if (file == NULL || file->instance_path == NULL) {
				ni_error("expansion error: cannot expand \"%%{%s}\" - no such file in this context", var_start);
				goto failed;
			}

			ni_stringbuf_puts(&buf, file->instance_path);
		} else {
			unsigned int j, var_len;
			const char *env_value = NULL;

			if (strchr(var_start, ':')) {
				ni_error("expansion error: cannot expand \"%%{%s}\" - unknown type", var_start);
				goto failed;
			}

			var_len = strlen(var_start);
			for (j = 0; j < env->count; ++j) {
				const char *ex = env->data[j];

				if (!strncmp(ex, var_start, var_len) && ex[var_len] == '=') {
					env_value = ex + var_len + 1;
					break;
				}
			}

			if (env_value == NULL) {
				ni_error("expansion error: cannot expand \"%%{%s}\" - environment variable not set in this context", var_start);
				goto failed;
			}

			ni_stringbuf_puts(&buf, env_value);
		}
	}

	ni_string_dup(sp, buf.string);
	ni_stringbuf_destroy(&buf);
	return TRUE;

failed:
	ni_stringbuf_destroy(&buf);
	return FALSE;
}

/*
 * Now export the filename to the environment.
 */
ni_bool_t
ni_testbus_agent_process_export_files(ni_process_t *pi, ni_testbus_file_array_t *files)
{
	char namebuf[256];
	unsigned int i;

	for (i = 0; i < files->count; ++i) {
		ni_testbus_file_t *file = files->data[i];

		if (file->instance_path) {
			snprintf(namebuf, sizeof(namebuf), "testbus_file_%s", file->name);
			ni_process_setenv(pi, namebuf, file->instance_path);
		}
	}

	/* Do file path substitution on the command line */
	for (i = 0; i < pi->argv.count; ++i) {
		if (!ni_testbus_agent_substitute(&pi->argv.data[i], &pi->environ, files))
			return FALSE;
	}

	return TRUE;
}

/*
 * We get here when the master deleted a file object.
 */
void
ni_testbus_agent_discard_cached_file(const char *object_path)
{
	unsigned int i, j;

	for (i = j = 0; i < global_files.count; ++i) {
		ni_testbus_file_t *file = global_files.data[i];

		if (ni_string_eq(file->object_path, object_path)) {
			ni_debug_testbus("file cache: discarding file %s (inum %u)", file->name, file->inum);
			ni_testbus_file_put(file);
		} else {
			global_files.data[j++] = file;
		}
	}

	global_files.count = j;
}

/*
 * Prior to running a process, modify the environment that we pass to it.
 * All user environment variables should be prefixed with "testbus_" to
 * avoid name space collisions.
 */
void
ni_testbus_agent_process_frob_environ(ni_process_t *pi)
{
	unsigned int i;

	for (i = 0; i < pi->environ.count; ++i) {
		ni_stringbuf_t sb = NI_STRINGBUF_INIT_DYNAMIC;

		ni_stringbuf_printf(&sb, "testbus_%s", pi->environ.data[i]);
		free(pi->environ.data[i]);
		pi->environ.data[i] = sb.string;
	}
}
