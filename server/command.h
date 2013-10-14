
#ifndef __SERVER_COMMAND_H__
#define __SERVER_COMMAND_H__

#include "container.h"

struct ni_testbus_command {
	ni_string_array_t		argv;
	ni_testbus_container_t		context;
};

struct ni_testbus_process {
	ni_testbus_command_t *		command;

	ni_string_array_t		argv;			/* argv, with substitution */
	ni_testbus_container_t		context;

	ni_process_t *			process;		/* internal process state */
};

extern void			ni_testbus_command_array_init(ni_testbus_command_array_t *);
extern void			ni_testbus_command_array_destroy(ni_testbus_command_array_t *);
extern void			ni_testbus_command_array_append(ni_testbus_command_array_t *, ni_testbus_command_t *);
extern ni_bool_t		ni_testbus_command_array_remove(ni_testbus_command_array_t *, const ni_testbus_command_t *);

extern ni_testbus_command_t *	ni_testbus_command_new(ni_testbus_container_t *, const ni_string_array_t *argv);
extern ni_testbus_command_t *	ni_testbus_command_cast(ni_testbus_container_t *);

extern void			ni_testbus_process_array_init(ni_testbus_process_array_t *);
extern void			ni_testbus_process_array_destroy(ni_testbus_process_array_t *);
extern void			ni_testbus_process_array_append(ni_testbus_process_array_t *, ni_testbus_process_t *);
extern ni_bool_t		ni_testbus_process_array_remove(ni_testbus_process_array_t *, const ni_testbus_process_t *);

extern ni_testbus_process_t *	ni_testbus_process_new(ni_testbus_container_t *, ni_testbus_command_t *);
extern ni_testbus_process_t *	ni_testbus_process_cast(ni_testbus_container_t *);
extern void			ni_testbus_process_apply_context(ni_testbus_process_t *, ni_testbus_container_t *);
extern ni_bool_t		ni_testbus_process_run(ni_testbus_process_t *, void (*)(ni_process_t *), void *);

static inline ni_testbus_command_t *
ni_testbus_command_get(ni_testbus_command_t *command)
{
	ni_testbus_container_get(&command->context);
	return command;
}

static inline void
ni_testbus_command_put(ni_testbus_command_t *command)
{
	ni_testbus_container_put(&command->context);
}

static inline ni_testbus_process_t *
ni_testbus_process_get(ni_testbus_process_t *process)
{
	ni_testbus_container_get(&process->context);
	return process;
}

static inline void
ni_testbus_process_put(ni_testbus_process_t *process)
{
	ni_testbus_container_put(&process->context);
}

#endif /* __SERVER_COMMAND_H__ */
