/*
 * Copyright (C) 2013 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <dborb/netinfo.h>
#include <dborb/logging.h>
#include <dborb/socket.h>
#include <dborb/dbus-errors.h>
#include <dborb/dbus-model.h>
#include <dborb/process.h>
#include <dborb/xml.h>
#include <dborb/buffer.h>
#include <testbus/model.h>
#include <testbus/client.h>
#include <testbus/process.h>
#include <testbus/file.h>

#include "dbus-filesystem.h"
#include "monitor.h"
#include "files.h"


/*
 * After the process has finished, upload the output
 */
static void
ni_testbus_agent_upload_output(ni_dbus_object_t *proc_object, const char *filename,
			ni_buffer_chain_t **chain, ni_testbus_file_t *file)
{
	ni_dbus_object_t *file_object;
	ni_buffer_t *bp;

	if (ni_buffer_chain_count(*chain) == 0)
		return;

	if (file && file->object_path) {
		file_object = ni_testbus_client_get_and_refresh_object(file->object_path);
	} else {
		file_object = ni_testbus_client_create_tempfile(filename, NI_TESTBUS_FILE_READ, proc_object);
		if (file_object == NULL)
			goto failed;
	}

	ni_debug_testbus("%s(%s, %s, %u bytes)", __func__, proc_object->path, filename,
			ni_buffer_chain_count(*chain));
	while ((bp = ni_buffer_chain_get_next(chain)) != NULL) {
		if (!ni_testbus_client_upload_file(file_object, bp)) {
			ni_buffer_free(bp);
			goto failed;
		}
		ni_buffer_free(bp);
	}

	return;

failed:
	ni_error("%s: failed to upload %s", proc_object->path, filename);
	return;
}

/*
 * Callback function for processes
 */
struct __ni_testbus_process_context {
	ni_dbus_server_t *	server;
	char *			object_path;
	ni_testbus_file_array_t *files;

	struct {
		ni_testbus_file_t *file;
		ni_buffer_chain_t *buffers;
	} stdout, stderr;
};

static struct __ni_testbus_process_context *
__ni_testbus_process_context_new(const char *master_object_path)
{
	struct __ni_testbus_process_context *ctx;

	ctx = ni_calloc(1, sizeof(*ctx));
//	ctx->server = ni_dbus_object_get_server(object);
	ctx->object_path = ni_strdup(master_object_path);
	return ctx;
}

static void
__ni_testbus_process_context_free(struct __ni_testbus_process_context *ctx)
{
	ni_buffer_chain_discard(&ctx->stdout.buffers);
	ni_buffer_chain_discard(&ctx->stderr.buffers);
	if (ctx->stdout.file)
		ni_testbus_file_put(ctx->stdout.file);
	if (ctx->stderr.file)
		ni_testbus_file_put(ctx->stderr.file);
	ni_string_free(&ctx->object_path);

	if (ctx->files)
		ni_testbus_file_array_free(ctx->files);
	free(ctx);
}

static void
__ni_testbus_process_notify(const char *master_object_path, ni_process_exit_info_t *exit_info,
				struct __ni_testbus_process_context *ctx)
{
	ni_dbus_object_t *proc_object;

	proc_object = ni_testbus_client_get_and_refresh_object(master_object_path);

	if (ctx) {
		ni_testbus_agent_upload_output(proc_object, "stdout", &ctx->stdout.buffers, ctx->stdout.file);
		ni_testbus_agent_upload_output(proc_object, "stderr", &ctx->stderr.buffers, ctx->stderr.file);
	}

	ni_testbus_client_process_exit(proc_object, exit_info);
}

static void
__ni_testbus_process_exit_notify(ni_process_t *pi)
{
	struct __ni_testbus_process_context *ctx = pi->user_data;
	ni_process_exit_info_t exit_info;

	ni_debug_testbus("process %s exited", ctx->object_path);
	ni_process_get_exit_info(pi, &exit_info);

	/* Now poll all event monitors to see whether there are
	 * new events. Then, push all pending events to the server */
	ni_testbus_agent_monitors_poll();
	ni_testbus_agent_eventlog_flush();

	__ni_testbus_process_notify(ctx->object_path, &exit_info, ctx);

	__ni_testbus_process_context_free(ctx);
	pi->user_data = NULL;

	ni_process_free(pi);
}

static void
__ni_testbus_process_read_notify(ni_process_t *pi, ni_process_buffer_t *pbf)
{
	struct __ni_testbus_process_context *ctx = pi->user_data;
	ni_buffer_chain_t **chain = NULL;

	if (pbf == &pi->stdout)
		chain = &ctx->stdout.buffers;
	else
	if (pbf == &pi->stderr)
		chain = &ctx->stderr.buffers;

	if (chain != NULL) {
		ni_buffer_chain_append(chain, pbf->wbuf);
		pbf->wbuf = NULL;
	}

	/* Future extension: signal the master that we have data.
	 * This would allow continuous streaming of the process output,
	 * rather than transferring everything in bulk on process exit. */
}

static ni_bool_t
__ni_testbus_process_run(ni_process_t *pi, const char *master_object_path, ni_testbus_file_array_t *files)
{
	struct __ni_testbus_process_context *ctx;
	ni_testbus_file_t *f;

	ni_testbus_agent_process_frob_environ(pi);

	if (files) {
		if (!ni_testbus_agent_process_attach_files(pi, files)
		 || !ni_testbus_agent_process_export_files(pi, files)) {
			ni_error("process %u: failed to attach files", pi->pid);
			return FALSE;
		}
	}

	if (ni_process_run(pi) < 0)
		return FALSE;

	ctx = __ni_testbus_process_context_new(master_object_path);

	if ((f = ni_testbus_file_array_find_by_name(files, "stdout")) != NULL)
		ctx->stdout.file = ni_testbus_file_get(f);
	if ((f = ni_testbus_file_array_find_by_name(files, "stderr")) != NULL)
		ctx->stderr.file = ni_testbus_file_get(f);

	pi->exit_callback = __ni_testbus_process_exit_notify;
	pi->read_callback = __ni_testbus_process_read_notify;
	pi->user_data = ctx;

	return TRUE;
}

ni_bool_t
ni_testbus_agent_run_command(ni_process_t *pi, const char *master_object_path, ni_testbus_file_array_t *files)
{
	if (!__ni_testbus_process_run(pi, master_object_path, files)) {
		ni_process_exit_info_t exit_info = { .how = NI_PROCESS_NONSTARTER };

		__ni_testbus_process_notify(master_object_path, &exit_info, NULL);
		ni_testbus_file_array_free(files);
		ni_process_free(pi);
		return FALSE;
	}

	return TRUE;
}

