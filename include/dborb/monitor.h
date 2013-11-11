#ifndef __TESTBUS_DBORB_MONITOR_H__
#define __TESTBUS_DBORB_MONITOR_H__

#include <sys/time.h>
#include <dborb/types.h>
#include <dborb/logging.h>

typedef struct ni_event_class {
	const char *		name;
	ni_bool_t		(*check_for_events)(ni_monitor_t *);
	void			(*destroy)(ni_monitor_t *);

	unsigned int		max_type;
	const char **		type_names;
} ni_event_class_t;

struct ni_event {
	char *			class;
	char *			source;
	char *			type;

	unsigned int		sequence;
	struct timeval		timestamp;
	ni_buffer_t *		data;
};

typedef struct ni_event_array {
	unsigned int		count;
	ni_event_t *		data;
} ni_event_array_t;

struct ni_eventlog {
	unsigned int		consumed;
	unsigned int		seqno;
	ni_event_array_t	events;
};

struct ni_monitor {
	unsigned int		refcount;
	char *			name;
	const ni_event_class_t *class;

	unsigned int		interval;	/* 0 if monitor has its own polling method */
	ni_bool_t		push;		/* true iff we should actively push new messages */
	ni_eventlog_t *		log;
};

typedef struct ni_monitor_array {
	unsigned int		count;
	ni_monitor_t **		data;
} ni_monitor_array_t;

void				ni_event_destroy(ni_event_t *);

ni_eventlog_t *			ni_eventlog_new(void);
void				ni_eventlog_free(ni_eventlog_t *);
unsigned int			ni_eventlog_pending_count(const ni_eventlog_t *);
const ni_event_t *		ni_eventlog_consume(ni_eventlog_t *);
void				ni_eventlog_prune(ni_eventlog_t *);
void				ni_eventlog_flush(ni_eventlog_t *);
void				ni_eventlog_add_event(ni_eventlog_t *, const ni_monitor_t *source, unsigned int type, ni_buffer_t *data);
const ni_event_t *		ni_eventlog_last(const ni_eventlog_t *);

void				ni_monitor_add_event(ni_monitor_t *, unsigned int, ni_buffer_t *);
void				ni_monitor_free(ni_monitor_t *);
ni_bool_t			ni_monitor_poll(ni_monitor_t *);

ni_monitor_t *			ni_file_monitor_new(const char *name, const char *path, ni_eventlog_t *);

void				ni_event_array_init(ni_event_array_t *);
void				ni_event_array_destroy(ni_event_array_t *);
ni_event_t *			ni_event_array_add(ni_event_array_t *);

void				ni_monitor_array_init(ni_monitor_array_t *);
void				ni_monitor_array_destroy(ni_monitor_array_t *);
void				ni_monitor_array_append(ni_monitor_array_t *, ni_monitor_t *);

static inline ni_monitor_t *
ni_monitor_get(ni_monitor_t *mon)
{
	ni_assert(mon->refcount);
	mon->refcount++;
	return mon;
}

static inline void
ni_monitor_put(ni_monitor_t *mon)
{
	ni_assert(mon->refcount);
	if (--(mon->refcount) == 0)
		ni_monitor_free(mon);
}

#endif /* __TESTBUS_DBORB_MONITOR_H__ */
