/*
 * Execute the requested process (almost) as if it were a
 * setuid process
 *
 * Copyright (C) 2002-2014 Olaf Kirch <okir@suse.de>
 */

#ifndef __WICKED_PROCESS_H__
#define __WICKED_PROCESS_H__

#include <dborb/logging.h>
#include <dborb/util.h>

struct ni_shellcmd {
	unsigned int		refcount;

	char *			command;

	ni_string_array_t	argv;
	ni_string_array_t	environ;

	unsigned int		timeout;
};

typedef struct ni_process_buffer ni_process_buffer_t;
struct ni_process_buffer {
	ni_bool_t		active;
	int			master_fd, slave_fd;
	ni_socket_t *		socket;
	ni_buffer_t *		wbuf;
	unsigned int		low_water_mark;

	ni_process_t *		process;		/* back pointer */
};

/*
 * Process exit info
 */
typedef enum {
	NI_PROCESS_NONSTARTER,
	NI_PROCESS_EXITED,
	NI_PROCESS_CRASHED,
	NI_PROCESS_TRANSCENDED,	/* anything else */
} ni_process_exit_mode_t;

struct ni_process_exit_info {
	ni_process_exit_mode_t	how;

	struct {
		int		code;
	} exit;
	struct {
		int		signal;
		ni_bool_t	core_dumped;
	} crash;

	/* TBD: stderr/stdout */
	unsigned int		stdout_bytes;
	unsigned int		stderr_bytes;
};

/*
 * Process structure controlling the running subprocess
 */
struct ni_process {
	ni_shellcmd_t *		process;

	pid_t			pid;
	int			status;
	ni_process_exit_info_t	exit_info;

	ni_string_array_t	argv;
	ni_string_array_t	environ;

	ni_bool_t		use_terminal;

	ni_process_buffer_t	stdin, stdout, stderr;

	ni_tempstate_t *	temp_state;

	void			(*read_callback)(ni_process_t *, ni_process_buffer_t *);
	void			(*exit_callback)(ni_process_t *);
	void *			user_data;
};

extern ni_shellcmd_t *		ni_shellcmd_new(const ni_string_array_t *argv);
extern ni_shellcmd_t *		ni_shellcmd_parse(const char *command);
extern ni_bool_t		ni_shellcmd_add_arg(ni_shellcmd_t *, const char *);

extern ni_process_t *		ni_process_new(ni_bool_t use_default_env);
extern ni_process_t *		ni_process_new_shellcmd(ni_shellcmd_t *);
extern ni_process_t *		ni_process_new_ext(const ni_string_array_t *, const ni_var_array_t *);
extern int			ni_process_run(ni_process_t *);
extern int			ni_process_run_and_wait(ni_process_t *);
extern int			ni_process_run_and_capture_output(ni_process_t *, ni_buffer_t *);
extern void			ni_process_setenv(ni_process_t *, const char *, const char *);
extern const char *		ni_process_getenv(const ni_process_t *, const char *);
extern ni_tempstate_t *		ni_process_tempstate(ni_process_t *);
extern void			ni_process_free(ni_process_t *);
extern void			ni_process_set_exit_info(ni_process_t *, const ni_process_exit_info_t *);
extern void			ni_process_get_exit_info(const ni_process_t *, ni_process_exit_info_t *);
extern int			ni_process_exit_status_okay(const ni_process_t *);
extern void			ni_shellcmd_free(ni_shellcmd_t *);

extern void			ni_process_capture_stdout(ni_process_t *);
extern void			ni_process_capture_stderr(ni_process_t *);
extern ni_bool_t		ni_process_attach_input_path(ni_process_t *, const char *filename);

static inline ni_shellcmd_t *
ni_shellcmd_hold(ni_shellcmd_t *proc)
{
	ni_assert(proc->refcount);
	proc->refcount++;
	return proc;
}

static inline void
ni_shellcmd_release(ni_shellcmd_t *proc)
{
	if (!proc)
		return;
	ni_assert(proc->refcount);
	if (--(proc->refcount) == 0)
		ni_shellcmd_free(proc);
}

#endif /* __WICKED_PROCESS_H__ */
