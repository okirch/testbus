/*
 * Execute the requested process (almost) as if it were a setuid process.
 *
 * Copyright (C) 2002-2012 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <dborb/logging.h>
#include <dborb/socket.h>
#include <dborb/process.h>
#include "socket_priv.h"
#include "util_priv.h"

static int				__ni_process_run(ni_process_t *);
static ni_socket_t *			__ni_process_get_output(ni_process_t *, int);
static void				__ni_process_flush_buffer(ni_process_t *, struct ni_process_buffer *);
static void				__ni_process_fill_exit_info(ni_process_t *);
static const ni_string_array_t *	__ni_default_environment(void);

static inline ni_bool_t
__ni_shellcmd_parse(ni_string_array_t *argv, const char *command)
{
	if (ni_string_split(argv, command, " \t", 0) == 0)
		return FALSE;
	return TRUE;
}

static inline const char *
__ni_shellcmd_format(char **cmd, const ni_string_array_t *argv)
{
	return ni_string_join(cmd, argv, " ");
}

static void
__ni_shellcmd_free(ni_shellcmd_t *proc)
{
	ni_string_array_destroy(&proc->environ);
	ni_string_free(&proc->command);
	free(proc);
}


/*
 * Create a process description
 */
ni_shellcmd_t *
ni_shellcmd_new(const ni_string_array_t *argv)
{
	ni_shellcmd_t *proc;
	unsigned int i;

	ni_assert(argv != NULL);

	proc = xcalloc(1, sizeof(*proc));

	for (i = 0; i < argv->count; ++i) {
		const char *arg = argv->data[i];

		if (ni_string_len(arg) == 0)
			continue;	/* fail ?! */

		if (ni_string_array_append(&proc->argv, arg) < 0) {
			__ni_shellcmd_free(proc);
			return NULL;
		}
	}
	if (__ni_shellcmd_format(&proc->command, &proc->argv) == NULL) {
		__ni_shellcmd_free(proc);
		return NULL;
	}
	if (ni_string_array_copy(&proc->environ, __ni_default_environment()) < 0) {
		__ni_shellcmd_free(proc);
		return NULL;
	}

	proc->refcount = 1;
	return proc;
}

ni_shellcmd_t *
ni_shellcmd_parse(const char *command)
{
	ni_shellcmd_t *proc;

	ni_assert(command != NULL);

	proc = xcalloc(1, sizeof(*proc));

	ni_string_dup(&proc->command, command);
	if (!__ni_shellcmd_parse(&proc->argv, proc->command)) {
		__ni_shellcmd_free(proc);
		return NULL;
	}
	if (ni_string_array_copy(&proc->environ, __ni_default_environment()) < 0) {
		__ni_shellcmd_free(proc);
		return NULL;
	}

	proc->refcount = 1;
	return proc;
}

void
ni_shellcmd_free(ni_shellcmd_t *proc)
{
	ni_assert(proc->refcount == 0);
	__ni_shellcmd_free(proc);
}

ni_bool_t
ni_shellcmd_add_arg(ni_shellcmd_t *proc, const char *arg)
{
	if (proc == NULL || ni_string_len(arg) == 0)
		return FALSE;

	if (ni_string_array_append(&proc->argv, arg) < 0)
		return FALSE;

	if (__ni_shellcmd_format(&proc->command, &proc->argv) == NULL)
		return FALSE;

	return TRUE;
}

/*
 * Process handling code
 */
void
ni_process_buffer_init(struct ni_process_buffer *pb, ni_process_t *pi)
{
	memset(pb, 0, sizeof(*pb));
	pb->low_water_mark = 4096;
	pb->fdpair[0] = -1;
	pb->fdpair[1] = -1;
	pb->process = pi;
}

static ni_bool_t
ni_process_buffer_open_pipe(struct ni_process_buffer *pb)
{
	if (pb->capture) {
		/* Our code in socket.c is only able to deal with sockets for now; */
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, pb->fdpair) < 0) {
			ni_error("%s: unable to create stdout pipe: %m", __func__);
			return FALSE;
		}
	}

	return TRUE;
}

static ni_bool_t
ni_process_buffer_prepare_writer(struct ni_process_buffer *pb, int destfd)
{
	if (pb->capture) {
		ni_assert(pb->fdpair[1] >= 0);

		if (dup2(pb->fdpair[1], destfd) < 0)
			goto failed;
	} else {
		int fd;

		if ((fd = open("/dev/null", O_RDONLY)) < 0) {
			ni_warn("%s: unable to open /dev/null: %m", __func__);
			return FALSE;
		}
		if (fd != destfd) {
			if (dup2(fd, destfd) < 0)
				goto failed;
			close(fd);
		}
	}

	return TRUE;

failed:
	ni_warn("%s: cannot dup %s descriptor: %m", __func__, destfd == 1? "stdout" : "stderr");
	return FALSE;
}

static ni_bool_t
ni_process_buffer_prepare_reader(struct ni_process_buffer *pb, ni_process_t *pi)
{
	if (pb->capture) {
		ni_assert(pb->fdpair[0] >= 0);

		pb->socket = __ni_process_get_output(pi, pb->fdpair[0]);
		ni_socket_activate(pb->socket);
		close(pb->fdpair[1]);

		pb->fdpair[0] = pb->fdpair[1] = -1;
	}

	return TRUE;
}

void
ni_process_buffer_destroy(struct ni_process_buffer *pb)
{
	if (pb->socket != NULL) {
		ni_socket_close(pb->socket);
		pb->socket = NULL;
	}
	if (pb->wbuf)
		ni_buffer_free(pb->wbuf);

	if (pb->fdpair[0] >= 0) {
		close(pb->fdpair[0]);
		pb->fdpair[0] = -1;
	}
	if (pb->fdpair[1] >= 0) {
		close(pb->fdpair[1]);
		pb->fdpair[1] = -1;
	}

	ni_process_buffer_init(pb, NULL);
}

ni_process_t *
ni_process_new(ni_bool_t use_default_env)
{
	ni_process_t *pi;

	pi = xcalloc(1, sizeof(*pi));
	pi->stdin = -1;

	ni_process_buffer_init(&pi->stdout, pi);
	ni_process_buffer_init(&pi->stderr, pi);

	if (use_default_env)
		ni_string_array_copy(&pi->environ, __ni_default_environment());

	return pi;
}

ni_process_t *
ni_process_new_ext(const ni_string_array_t *argv, const ni_var_array_t *env)
{
	ni_process_t *pi;
	unsigned int i;

	pi = ni_process_new(FALSE);

	/* Copy the command array */
	ni_string_array_copy(&pi->argv, argv);

	/* Copy the environment */
	for (i = 0; i < env->count; ++i) {
		const ni_var_t *var = &env->data[i];

		ni_process_setenv(pi, var->name, var->value);
	}

	return pi;
}

ni_process_t *
ni_process_new_shellcmd(ni_shellcmd_t *proc)
{
	ni_process_t *pi;

	pi = ni_process_new(TRUE);
	ni_string_array_copy(&pi->argv, &proc->argv);
	ni_string_array_copy(&pi->environ, &proc->environ);
	pi->process = ni_shellcmd_hold(proc);

	return pi;
}

void
ni_process_free(ni_process_t *pi)
{
	if (pi->pid) {
		if (kill(pi->pid, SIGKILL) < 0)
			ni_error("Unable to kill process %d (%s): %m", pi->pid, pi->process->command);
	}

	ni_process_buffer_destroy(&pi->stdout);
	ni_process_buffer_destroy(&pi->stderr);

	if (pi->temp_state != NULL) {
		ni_tempstate_finish(pi->temp_state);
		pi->temp_state = NULL;
	}

	if (pi->stdin >= 0)
		close(pi->stdin);

	ni_string_array_destroy(&pi->argv);
	ni_string_array_destroy(&pi->environ);
	ni_shellcmd_release(pi->process);
	free(pi);
}

/*
 * Setting environment variables
 */
static void
__ni_process_setenv(ni_string_array_t *env, const char *name, const char *value)
{
	unsigned int namelen = strlen(name), totlen;
	unsigned int i;
	char *newvar;

	totlen = namelen + strlen(value) + 2;
	newvar = malloc(totlen);
	snprintf(newvar, totlen, "%s=%s", name, value);

	for (i = 0; i < env->count; ++i) {
		char *oldvar = env->data[i];

		if (!strncmp(oldvar, name, namelen) && oldvar[namelen] == '=') {
			env->data[i] = newvar;
			free(oldvar);
			return;
		}
	}

	ni_string_array_append(env, newvar);
	free(newvar);
}

void
ni_shellcmd_setenv(ni_shellcmd_t *proc, const char *name, const char *value)
{
	__ni_process_setenv(&proc->environ, name, value);
}

void
ni_process_setenv(ni_process_t *pi, const char *name, const char *value)
{
	__ni_process_setenv(&pi->environ, name, value);
}

/*
 * Getting environment variables
 */
static const char *
__ni_process_getenv(const ni_string_array_t *env, const char *name)
{
	unsigned int namelen = strlen(name);
	unsigned int i;

	for (i = 0; i < env->count; ++i) {
		char *oldvar = env->data[i];

		if (!strncmp(oldvar, name, namelen) && oldvar[namelen] == '=') {
			oldvar += namelen + 1;
			return oldvar[0]? oldvar : NULL;
		}
	}

	return NULL;
}

const char *
ni_process_getenv(const ni_process_t *pi, const char *name)
{
	return __ni_process_getenv(&pi->environ, name);
}

/*
 * Populate default environment
 */
static const ni_string_array_t *
__ni_default_environment(void)
{
	static ni_string_array_t defenv;
	static int initialized = 0;
	static const char *copy_env[] = {
		"LD_LIBRARY_PATH",
		"LD_PRELOAD",
		"PATH",

		NULL,
	};

	if (!initialized) {
		const char **envpp, *name;

		for (envpp = copy_env; (name = *envpp) != NULL; ++envpp) {
			const char *value;

			if ((value = getenv(name)) != NULL)
				__ni_process_setenv(&defenv, name, value);
		}
		initialized = 1;
	}

	return &defenv;
}

/*
 * Create a temp state for this process; this state will track
 * temporary resources like tempfiles
 */
ni_tempstate_t *
ni_process_tempstate(ni_process_t *process)
{
	if (process->temp_state == NULL)
		process->temp_state = ni_tempstate_new(NULL);

	return process->temp_state;
}

/*
 * Catch sigchild
 */
static void
ni_process_sigchild(int sig)
{
	/* nop for now */
}

/*
 * Run a subprocess.
 */
int
ni_process_run(ni_process_t *pi)
{
	int rv;

	if (!ni_process_buffer_open_pipe(&pi->stdout))
		return -1;

	if (!ni_process_buffer_open_pipe(&pi->stderr))
		return -1;

	rv = __ni_process_run(pi);
	if (rv >= 0) {
		/* Set up a socket to receive the redirected output of the
		 * subprocess. */
		ni_process_buffer_prepare_reader(&pi->stdout, pi);
		ni_process_buffer_prepare_reader(&pi->stderr, pi);
	}

	return rv;
}

int
ni_process_run_and_wait(ni_process_t *pi)
{
	int  rv;

	rv = __ni_process_run(pi);
	if (rv < 0)
		return rv;

	while (waitpid(pi->pid, &pi->status, 0) < 0) {
		if (errno == EINTR)
			continue;
		ni_error("%s: waitpid returns error (%m)", __func__);
		return -1;
	}

	__ni_process_flush_buffer(pi, &pi->stdout);
	__ni_process_flush_buffer(pi, &pi->stderr);

	pi->pid = 0;
	if (pi->exit_callback)
		pi->exit_callback(pi);

	if (!ni_process_exit_status_okay(pi)) {
		ni_error("subprocesses exited with error");
		return -1;
	}

	return rv;
}

#ifdef currently_broken
int
ni_process_run_and_capture_output(ni_process_t *pi, ni_buffer_t *out_buffer)
{
	int rv;

	if (!ni_process_buffer_open_pipe(&pi->stdout))
		return -1;

	rv = __ni_process_run(pi);
	if (rv < 0)
		return rv;

	close(pfd[1]);
	while (1) {
		int cnt;

		if (ni_buffer_tailroom(out_buffer) < 256)
			ni_buffer_ensure_tailroom(out_buffer, 4096);

		cnt = read(pfd[0], ni_buffer_tail(out_buffer), ni_buffer_tailroom(out_buffer));
		if (cnt == 0) {
			break;
		} else if (cnt > 0) {
			out_buffer->tail += cnt;
		} else if (errno != EINTR) {
			ni_error("read error on subprocess pipe: %m");
			return -1;
		}
	}

	while (waitpid(pi->pid, &pi->status, 0) < 0) {
		if (errno == EINTR)
			continue;
		ni_error("%s: waitpid returns error (%m)", __func__);
		return -1;
	}

	pi->pid = 0;
	if (pi->exit_callback)
		pi->exit_callback(pi);

	if (!ni_process_exit_status_okay(pi)) {
		ni_error("subprocesses exited with error");
		return -1;
	}

	return rv;
}
#endif

int
__ni_process_run(ni_process_t *pi)
{
	const char *arg0 = pi->argv.data[0];
	pid_t pid;

	if (pi->pid != 0) {
		ni_error("Cannot execute process instance twice (%s)", pi->process->command);
		return -1;
	}

	if (!ni_file_executable(arg0)) {
		ni_error("Unable to run %s; does not exist or is not executable", arg0);
		return -1;
	}

	signal(SIGCHLD, ni_process_sigchild);

	if ((pid = fork()) < 0) {
		ni_error("%s: unable to fork child process: %m", __func__);
		return -1;
	}
	pi->pid = pid;

	if (pid == 0) {
		int maxfd;
		int fd;

		if (chdir("/") < 0)
			ni_warn("%s: unable to chdir to /: %m", __func__);

		if ((fd = pi->stdin) < 0) {
			if ((fd = open("/dev/null", O_RDONLY)) < 0)
				ni_warn("%s: unable to open /dev/null: %m", __func__);
		}

		close(0);
		if (fd >= 0 && dup2(fd, 0) < 0)
			ni_warn("%s: cannot dup stdin descriptor: %m", __func__);

		ni_process_buffer_prepare_writer(&pi->stdout, 1);
		ni_process_buffer_prepare_writer(&pi->stderr, 2);

		maxfd = getdtablesize();
		for (fd = 3; fd < maxfd; ++fd)
			close(fd);

		/* NULL terminate argv and env lists */
		ni_string_array_append(&pi->argv, NULL);
		ni_string_array_append(&pi->environ, NULL);

		arg0 = pi->argv.data[0];
		execve(arg0, pi->argv.data, pi->environ.data);

		ni_fatal("%s: cannot execute %s: %m", __func__, arg0);
	}

	return 0;
}

/*
 * Collect the exit status of the child process
 */
static int
ni_process_reap(ni_process_t *pi)
{
	int rv;

	if (pi->pid == 0) {
		ni_error("%s: child already reaped", __func__);
		return 0;
	}

	rv = waitpid(pi->pid, &pi->status, WNOHANG);
	if (rv == 0) {
		/* This is an ugly workaround. Sometimes, we seem to get a hangup on the socket even
		 * though the script (provably) still has its end of the socket pair open for writing. */
		ni_error("%s: process %u has not exited yet; now doing a blocking waitpid()", __func__, pi->pid);
		rv = waitpid(pi->pid, &pi->status, 0);
	}

	if (rv < 0) {
		ni_error("%s: waitpid returns error (%m)", __func__);
		return -1;
	}

	__ni_process_fill_exit_info(pi);

	__ni_process_flush_buffer(pi, &pi->stdout);
	__ni_process_flush_buffer(pi, &pi->stderr);

	if (ni_debug & NI_TRACE_EXTENSION) {
		const char *cmd;

		cmd = pi->process? pi->process->command : pi->argv.data[0];
		if (WIFEXITED(pi->status))
			ni_debug_extension("subprocess %d (%s) exited with status %d",
					pi->pid, cmd,
					WEXITSTATUS(pi->status));
		else if (WIFSIGNALED(pi->status))
			ni_debug_extension("subprocess %d (%s) died with signal %d%s",
					pi->pid, cmd,
					WTERMSIG(pi->status),
					WCOREDUMP(pi->status)? " (core dumped)" : "");
		else
			ni_debug_extension("subprocess %d (%s) transcended into nirvana",
					pi->pid, cmd);
	}

	pi->pid = 0;

	if (pi->exit_callback)
		pi->exit_callback(pi);

	return 0;
}

int
ni_process_exit_status_okay(const ni_process_t *pi)
{
	if (WIFEXITED(pi->status))
		return WEXITSTATUS(pi->status) == 0;

	return 0;
}

void
ni_process_set_exit_info(ni_process_t *pi, const ni_process_exit_info_t *exit_info)
{
	pi->exit_info = *exit_info;
}

void
ni_process_get_exit_info(const ni_process_t *pi, ni_process_exit_info_t *exit_info)
{
	*exit_info = pi->exit_info;
}

void
__ni_process_fill_exit_info(ni_process_t *pi)
{
	ni_process_exit_info_t *exit_info = &pi->exit_info;

	memset(exit_info, 0, sizeof(*exit_info));
	if (WIFEXITED(pi->status)) {
		exit_info->how = NI_PROCESS_EXITED;
		exit_info->exit.code = WEXITSTATUS(pi->status);
	} else
	if (WIFSIGNALED(pi->status)) {
		exit_info->how = NI_PROCESS_CRASHED;
		exit_info->crash.signal = WTERMSIG(pi->status);
		exit_info->crash.core_dumped = !!WCOREDUMP(pi->status);
	} else {
		exit_info->how = NI_PROCESS_TRANSCENDED;
	}
}

/*
 * Connect the subprocess output to our I/O handling loop
 */
static void		__ni_process_output_recv(ni_socket_t *sock);

static void
__ni_process_flush_buffer(ni_process_t *pi, struct ni_process_buffer *pb)
{
	if (pb->socket)
		__ni_process_output_recv(pb->socket);
	if (pb->wbuf) {
		if (pi->read_callback)
			pi->read_callback(pi, pb);

		if (pb->wbuf) {
			ni_buffer_free(pb->wbuf);
			pb->wbuf = NULL;
		}
	}
}

static ni_process_buffer_t *
__ni_process_buffer_for_socket(ni_socket_t *sock)
{
	struct ni_process_buffer *pb;
	ni_process_t *pi = sock->user_data;

	ni_assert(pi);
	if (sock == pi->stdout.socket)
		return &pi->stdout;
	else if (sock == pi->stderr.socket)
		return &pi->stderr;
	return NULL;
}

static void
__ni_process_output_recv(ni_socket_t *sock)
{
	ni_process_t *pi = sock->user_data;
	struct ni_process_buffer *pb;
	ni_buffer_t *wbuf;
	int cnt;

	if (!(pb = __ni_process_buffer_for_socket(sock)))
		ni_fatal("%s: don't know this socket", __func__);

repeat:
	if (pb->wbuf == NULL)
		pb->wbuf = ni_buffer_new(4096);
	wbuf = pb->wbuf;

	cnt = recv(sock->__fd, ni_buffer_tail(wbuf), ni_buffer_tailroom(wbuf), MSG_DONTWAIT);
	if (cnt >= 0) {
		ni_bool_t notify = FALSE, repeat = FALSE;

		ni_buffer_push_tail(wbuf, cnt);

		if (ni_buffer_tailroom(wbuf) == 0)
			notify = repeat = TRUE;
		else if (ni_buffer_count(wbuf) >= pb->low_water_mark)
			notify = TRUE;

		if (notify)
			__ni_process_flush_buffer(pi, pb);
		if (repeat)
			goto repeat;
	} else if (errno != EWOULDBLOCK) {
		ni_error("read error on subprocess pipe: %m");
		ni_socket_deactivate(sock);
	}
}

static void
__ni_process_output_hangup(ni_socket_t *sock)
{
	ni_process_t *pi = sock->user_data;
	struct ni_process_buffer *pb;

	if (!(pb = __ni_process_buffer_for_socket(sock)))
		return;

	if (pb->socket == sock) {
		if (ni_process_reap(pi) < 0)
			ni_error("pipe closed by child process, but child did not exit");
		if (pb->socket)
			ni_socket_close(pb->socket);
		pb->socket = NULL;
	}
}

static ni_socket_t *
__ni_process_get_output(ni_process_t *pi, int fd)
{
	ni_socket_t *sock;

	sock = ni_socket_wrap(fd, SOCK_STREAM);
	sock->receive = __ni_process_output_recv;
	sock->handle_hangup = __ni_process_output_hangup;

	sock->user_data = pi;
	return sock;
}

