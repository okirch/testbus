
#include <stdlib.h>
#include <dborb/monitor.h>
#include <dborb/util.h>
#include <dborb/buffer.h>
#include <dborb/logging.h>

ni_eventlog_t *
ni_eventlog_new(void)
{
	ni_eventlog_t *log;

	log = ni_malloc(sizeof(*log));
	log->seqno = 1;
	return log;
}

void
ni_eventlog_free(ni_eventlog_t *log)
{
	ni_event_array_destroy(&log->events);
	free(log);
}

void
ni_eventlog_add_event(ni_eventlog_t *log, const ni_event_class_t *class, unsigned int type, ni_buffer_t *data)
{
	ni_event_t *ev;

	ev = ni_event_array_add(&log->events);
	ev->sequence = log->seqno++;
	ev->class = class;
	ev->type = type;
	ev->data = data;

	gettimeofday(&ev->timestamp, NULL);
}

void
ni_event_array_init(ni_event_array_t *array)
{
	memset(array, 0, sizeof(*array));
}

void
ni_event_array_destroy(ni_event_array_t *array)
{
	while (array->count--)
		ni_event_destroy(&array->data[array->count]);
	free(array->data);
	memset(array, 0, sizeof(*array));
}

ni_event_t *
ni_event_array_add(ni_event_array_t *array)
{
	ni_event_t *ev;

	array->data = ni_realloc(array->data, (array->count + 1) * sizeof(array->data[0]));
	ev = &array->data[array->count++];

	memset(ev, 0, sizeof(*ev));
	return ev;
}


void
ni_event_destroy(ni_event_t *ev)
{
	if (ev->data)
		ni_buffer_free(ev->data);
	memset(ev, 0, sizeof(*ev));
}


void
ni_monitor_init(ni_monitor_t *mon, const ni_event_class_t *class, const char *name, ni_eventlog_t *log)
{
	ni_string_dup(&mon->name, name);
	mon->class = class;
	mon->log = log;
}

void
ni_monitor_add_event(ni_monitor_t *mon, unsigned int type, ni_buffer_t *data)
{
	ni_eventlog_add_event(mon->log, mon->class, type, data);
}

void
ni_monitor_free(ni_monitor_t *mon)
{
	if (mon->class->destroy)
		mon->class->destroy(mon);

	ni_string_free(&mon->name);
	free(mon);
}

/*
 * File monitoring code
 */
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

enum {
	NI_FILEMON_EVENT_DATA,
	NI_FILEMON_EVENT_TRUNC,

	__NI_FILEMON_EVENT_MAX_TYPE
};

static const char *	__ni_filemon_event_names[__NI_FILEMON_EVENT_MAX_TYPE] = {
[NI_FILEMON_EVENT_DATA]	= "data",
[NI_FILEMON_EVENT_TRUNC]= "truncate",
};

typedef struct ni_file_monitor {
	ni_monitor_t		base;

	char *			pathname;

	int			fd;

	ni_bool_t		statbuf_valid;
	struct stat		statbuf;
} ni_file_monitor_t;

static ni_bool_t
ni_filemon_open(ni_file_monitor_t *filemon)
{
	filemon->fd = open(filemon->pathname, O_RDONLY);
	if (filemon->fd >= 0) {
		fstat(filemon->fd, &filemon->statbuf);
		filemon->statbuf_valid = TRUE;
		return TRUE;
	}
	return FALSE;
}

static void
ni_filemon_close(ni_file_monitor_t *filemon)
{
	if (filemon->fd >= 0)
		close(filemon->fd);
	filemon->fd = -1;
	filemon->statbuf_valid = FALSE;
}

static void
ni_filemon_log_data(ni_file_monitor_t *filemon, off_t from, off_t to)
{
	ni_buffer_t *data = ni_buffer_new(to - from);

	if (lseek(filemon->fd, from, SEEK_SET) < 0) {
		ni_error("%s: cannot seet to offset %ld", filemon->pathname, from);
		return;
	}

	while (TRUE) {
		unsigned int count;
		int n;

		if ((count = ni_buffer_tailroom(data)) == 0)
			break;

		n = read(filemon->fd, ni_buffer_tail(data), count);
		if (n < 0) {
			ni_error("%s: read error: %m", filemon->pathname);
			break;
		}
		if (n == 0)
			break;
		ni_buffer_push_tail(data, n);
	}

	ni_monitor_add_event(&filemon->base, NI_FILEMON_EVENT_DATA, data);
}

static ni_bool_t
ni_filemon_check_for_events(ni_monitor_t *mon)
{
	ni_file_monitor_t *filemon = ni_container_of(mon, ni_file_monitor_t, base);
	struct stat now, *old = &filemon->statbuf;
	ni_bool_t rv = FALSE;

	if (filemon->fd < 0) {
		if (!ni_filemon_open(filemon))
			return FALSE;
		ni_filemon_log_data(filemon, 0, old->st_size);
		return TRUE;
	}

	if (fstat(filemon->fd, &now) < 0) {
		ni_error("%s: cannot fstat: %m", filemon->pathname);
		ni_filemon_close(filemon);
		return FALSE;
	}

	if (filemon->statbuf_valid && old->st_size < now.st_size) {
		ni_filemon_log_data(filemon, old->st_size, now.st_size);
		rv = TRUE;
	}

	filemon->statbuf = now;
	filemon->statbuf_valid = TRUE;

	/* Now do a stat() on the pathname to see whether the file was closed
	 * and re-created */
	if (stat(filemon->pathname, &now) < 0) {
		/* file went away */
		ni_filemon_log_data(filemon, old->st_size, 0x7fffffff);
		ni_filemon_close(filemon);
		rv = TRUE;
	} else if (old->st_dev != now.st_dev || old->st_ino != now.st_ino) {
		ni_filemon_log_data(filemon, old->st_size, 0x7fffffff);
		ni_filemon_close(filemon);
		if (ni_filemon_open(filemon))
			ni_filemon_log_data(filemon, 0, old->st_size);
		rv = TRUE;
	}

	return rv;
}

static ni_event_class_t		ni_file_monitor_class = {
	.name			= "file",
	.check_for_events	= ni_filemon_check_for_events,

	.max_type		= __NI_FILEMON_EVENT_MAX_TYPE,
	.type_names		= __ni_filemon_event_names,
};

ni_monitor_t *
ni_file_monitor_new(const char *name, const char *path, ni_eventlog_t *log)
{
	ni_file_monitor_t *filemon;

	filemon = ni_malloc(sizeof(*filemon));
	ni_monitor_init(&filemon->base, &ni_file_monitor_class, name, log);
	filemon->base.interval = 5;

	ni_string_dup(&filemon->pathname, path);

	ni_filemon_open(filemon);

	return &filemon->base;
}
