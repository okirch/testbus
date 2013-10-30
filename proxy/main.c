/*
 * Program for proxying a DBus service to other hosts and virtualization guests.
 *
 * Copyright (C) 2013 Olaf Kirch <okir@suse.de>
 *
 * Use cases:
 *
 *  -	Run a dbus based agent on a remote host, and make it connect
 *	to the dbus-server on the originating host as if it were
 *	running locally:
 *	 dbus-proxy -- \
 *		ssh user@host dbus-proxy --upstream stdio -- testbus-agent
 *	The proxy on the originating host will ssh to the remote host,
 *	where another instance of the proxy is started. These two proxy
 *	instances will forward traffic via the ssh pipe.
 *
 *	On the originating host, the proxy will connect to the local
 *	(system) dbus.
 *
 *	On the remote end, the proxy will spawn "testbus-agent" and
 *	provide it with a DBUS_SESSION_BUS_ADDRESS. Consequently,
 *	testbus-agent should connect to the session bus rather than the
 *	system bus.
 *
 *  -	Run a dbus agent in an lxc container:
 *
 *	Before you start the lxc instance, set up the proxy on the
 *	host:
 *	 dbus-proxy --downstream unix:$path_to_container/var/run/dbus-proxy.socket
 *
 *	Inside the lxc guest, start your agent like this:
 *	 export DBUS_SESSION_BUS_ADDRESS=unix:/var/run/dbus-proxy.socket
 *	 testbus-agent
 *
 *  -	Run the agent in a KVM or XEN guest
 *
 *	[To be fleshed out]
 *
 *  -	Use a serial (null modem) line for communication; eg this would
 *	be used by the TAHI test suite.
 *
 *	[To be fleshed out]
 *
 */

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <dborb/logging.h>
#include <dborb/util.h>
#include <dborb/netinfo.h>
#include <dborb/buffer.h>

enum {
	OPT_HELP,
	OPT_VERSION,
	OPT_CONFIGFILE,
	OPT_DEBUG,
	OPT_LOG_LEVEL,
	OPT_LOG_TARGET,

	OPT_FOREGROUND,
	OPT_UPSTREAM,
	OPT_DOWNSTREAM,
	OPT_EXECUTE,
};

static struct option	options[] = {
	/* common */
	{ "help",		no_argument,		NULL,	OPT_HELP },
	{ "version",		no_argument,		NULL,	OPT_VERSION },
	{ "config",		required_argument,	NULL,	OPT_CONFIGFILE },
	{ "debug",		required_argument,	NULL,	OPT_DEBUG },
	{ "log-level",		required_argument,	NULL,	OPT_LOG_LEVEL },
	{ "log-target",		required_argument,	NULL,	OPT_LOG_TARGET },

	{ "foreground",		no_argument,		NULL,	OPT_FOREGROUND },
	{ "upstream",		required_argument,	NULL,	OPT_UPSTREAM },
	{ "downstream",		required_argument,	NULL,	OPT_DOWNSTREAM },

	{ NULL }
};

typedef struct io_mbuf	io_mbuf_t;
typedef struct io_endpoint io_endpoint_t;
typedef struct io_endpoint_list io_endpoint_list_t;
typedef struct io_transport io_transport_t;
typedef struct io_transport_ops io_transport_ops_t;

struct io_mbuf {
	io_mbuf_t *		next;
	io_endpoint_t *		source;
	ni_buffer_t *		buffer;
	unsigned int		credit;
};

typedef enum {
	IO_ENDPOINT_TYPE_NONE,
	IO_ENDPOINT_TYPE_SIMPLEX,
	IO_ENDPOINT_TYPE_MULTIPLEX,
} io_endpoint_type_t;

struct io_endpoint {
	io_endpoint_t *	next;

	io_endpoint_type_t type;
	io_transport_t *transport;

	io_endpoint_t *	sink;

	/* Channel ID 0 means this is a multiplexing end point.
	 * Everything else indicates the ID of this channel */
	uint32_t	channel_id;

	int		rfd, wfd;

	unsigned int	rx_credit;
	ni_buffer_t *	rbuf;		/* only used by fan-out endpoints */
	ni_buffer_t *	wbuf;

	io_mbuf_t *	wqueue;

	unsigned int	shutdown_write : 1,
			connected      : 1;

	char *		socket_name;
};

#define DEFAULT_CREDIT_SIMPLEX		8192
#define DEFAULT_CREDIT_MULTIPLEX	(16 * DEFAULT_CREDIT_SIMPLEX)

struct io_endpoint_list {
	io_endpoint_t *	head;
};

#define foreach_io_endpoint(ep, list) \
	for (ep = (list)->head; ep; ep = ep->next)

struct io_transport_ops {
	io_endpoint_t *	(*connector)(io_transport_t *);
	io_endpoint_t *	(*acceptor)(io_transport_t *, int fd);
	ni_bool_t	(*delayed_connector)(io_transport_t *, io_endpoint_t *);
	ni_bool_t	(*listen)(io_transport_t *);
};

struct io_transport {
	const char *	name;
	const io_transport_ops_t *ops;

	io_endpoint_type_t type;
	io_endpoint_t *	multiplex;
	io_endpoint_list_t ep_list;

	const char *	address;
	int		listen_fd;

	io_transport_t *other;
};

typedef struct proxy	proxy_t;
struct proxy {
	unsigned int	channel_id;

	io_transport_t	upstream;
	io_transport_t	downstream;
};

static const char *	program_name;
static const char *	opt_log_target;
static int		opt_foreground;
static char *		opt_upstream = "unix:/var/run/dbus/system_bus_socket";
static char *		opt_downstream;

static void		proxy_init(proxy_t *);
static int		proxy_connect(const char *);	/* FIXME: rename io_socket_connect() */
static int		do_exec(char **);
static void		do_proxy(proxy_t *);

static io_mbuf_t *	io_mbuf_wrap(ni_buffer_t *, io_endpoint_t *);
static void		io_mbuf_free(io_mbuf_t *);
static ni_buffer_t *	io_mbuf_take_buffer(io_mbuf_t *);

static int		io_socket_listen(const char *);

static void		io_endpoint_queue_write(io_endpoint_t *, io_mbuf_t *);
static void		io_endpoint_queue(io_endpoint_list_t *, io_endpoint_t *);

static io_endpoint_t *	io_endpoint_pipe_new(io_transport_t *xprt);
static io_endpoint_t *	io_endpoint_socket_new(io_transport_t *xprt);
static io_endpoint_t *	io_endpoint_serial_new(io_transport_t *xprt);
static ni_bool_t	io_endpoint_socket_listen(io_transport_t *xprt);
static io_endpoint_t *	io_endpoint_socket_accept(io_transport_t *xprt, int fd);
static void		io_endpoint_shutdown_source(io_endpoint_t *sink, io_endpoint_t *source);
static void		io_endpoint_free(io_endpoint_t *);

static ni_bool_t	io_transport_init(io_transport_t *xprt, const char *param_string);
static ni_bool_t	io_transport_listen(io_transport_t *xprt);
static io_endpoint_t *	io_transport_accept(io_transport_t *xprt, int fd);
static io_endpoint_t *	io_transport_connect(io_transport_t *xprt, ni_bool_t full);
static ni_bool_t	io_transport_connect_finish(io_transport_t *xprt, io_endpoint_t *ep);

static io_mbuf_t *	proxy_channel_open_new(unsigned int channel_id);
static io_mbuf_t *	proxy_channel_close_new(unsigned int channel_id);

int
main(int argc, char **argv)
{
	proxy_t proxy;
	char **opt_argv = NULL;
	int c;

	program_name = ni_basename(argv[0]);

	while ((c = getopt_long(argc, argv, "+", options, NULL)) != EOF) {
		switch (c) {
		default:
		usage:
		case OPT_HELP:
			fprintf(stderr,
				"%s [options] --downstream <streamspec>\n"
				"%s [options] -- session-cmd [session-options ...]\n"
				"This command understands the following options\n"
				"  --help\n"
				"  --version\n"
				"  --config filename\n"
				"        Read configuration file <filename> instead of system default.\n"
				"  --debug facility\n"
				"        Enable debugging for debug <facility>.\n"
				"        Use '--debug help' for a list of debug facilities.\n"
				"  --log-level level\n"
				"        Set log level to <error|warning|notice|info|debug>.\n"
				"  --log-target target\n"
				"        Set log destination to <stderr|syslog>.\n"
				"  --foreground\n"
				"        Tell the daemon to not background itself at startup.\n"
				, program_name
				, program_name);
			return (c == OPT_HELP ? 0 : 1);

		case OPT_VERSION:
			printf("%s %s\n", program_name, "0.1");
			return 0;

		case OPT_CONFIGFILE:
			ni_set_global_config_path(optarg);
			break;

		case OPT_DEBUG:
			if (!strcmp(optarg, "help")) {
				printf("Supported debug facilities:\n");
				ni_debug_help();
				return 0;
			}
			if (ni_enable_debug(optarg) < 0) {
				fprintf(stderr, "Bad debug facility \"%s\"\n", optarg);
				return 1;
			}
			break;

		case OPT_LOG_LEVEL:
			if (!ni_log_level_set(optarg)) {
				fprintf(stderr, "Bad log level \%s\"\n", optarg);
				return 1;
			}
			break;

		case OPT_LOG_TARGET:
			opt_log_target = optarg;
			break;

		case OPT_UPSTREAM:
			opt_upstream = optarg;
			break;

		case OPT_DOWNSTREAM:
			opt_downstream = optarg;
			break;

		case OPT_FOREGROUND:
			opt_foreground = 1;
			break;
		}
	}

	if (opt_downstream != NULL) {
		if (optind < argc) {
			ni_error("Session command specified and --downstream option are mutually exclusive");
			goto usage;
		}
	} else {
		if (optind == argc) {
			ni_error("No session command specified\n");
			goto usage;
		}
		opt_argv = argv + optind;
	}

	if (opt_log_target) {
		if (!ni_log_destination(program_name, opt_log_target)) {
			fprintf(stderr, "Bad log destination \%s\"\n",
					opt_log_target);
			return 1;
		}
	} else if (opt_foreground && getppid() != 1) {
		if (ni_debug) {
			ni_log_destination(program_name, "perror");
		} else {
			ni_log_destination(program_name, "syslog:perror");
		}
	} else {
		ni_log_destination(program_name, "syslog");
	}

	if (ni_init("proxy") < 0)
		return 1;

	proxy_init(&proxy);

	if (opt_upstream == NULL)
		opt_upstream = "unix:/var/run/dbus/system_bus_socket";

	if (!io_transport_init(&proxy.upstream, opt_upstream))
		ni_fatal("Unable to set up upstream transport");

	if (opt_downstream == NULL) {
#if 0
  		proxy.downstream.listen_fd = do_exec(opt_argv);
  		proxy.downstream.acceptor = proxy_accept;
#else
		ni_trace("exec not supported right now");
#endif
	} else {
		if (!io_transport_init(&proxy.downstream, opt_downstream))
			ni_fatal("Unable to set up downstream transport");
	}

	if (!io_transport_listen(&proxy.downstream))
		ni_fatal("failed to open downstream endpoint for listening");

	do_proxy(&proxy);
	return 0;
}

enum {
	CHANNEL_OPEN,
	CHANNEL_CLOSE,
	CHANNEL_DATA,

	__CHANNEL_CMD_COUNT
};

struct data_header {
	uint32_t	cmd;
	uint32_t	channel;
	uint32_t	count;
} __attribute__((packed));

#define DATA_HEADER_SIZE	sizeof(struct data_header)

typedef int (*io_handler_fn_t)(proxy_t *, io_endpoint_t *, const struct pollfd *);

struct pollinfo {
	io_handler_fn_t		handler;
	io_endpoint_t *		ep;
};

static inline void
__set_poll_info(struct pollinfo *pi, io_handler_fn_t handler, io_endpoint_t *ep)
{
	pi->handler= handler;
	pi->ep = ep;
}

void
io_mbuf_free(io_mbuf_t *mbuf)
{
	ni_buffer_t *bp;

	if ((bp = io_mbuf_take_buffer(mbuf)) != NULL)
		ni_buffer_free(bp);
	free(mbuf);
}

io_mbuf_t *
io_mbuf_wrap(ni_buffer_t *bp, io_endpoint_t *ep)
{
	io_mbuf_t *mbuf = ni_malloc(sizeof(*mbuf));

	mbuf->source = ep;
	mbuf->buffer = bp;
	mbuf->credit = ni_buffer_count(bp);
	return mbuf;
}

static ni_buffer_t *
io_mbuf_take_buffer(io_mbuf_t *mbuf)
{
	io_endpoint_t *source;
	ni_buffer_t *bp;

	if ((bp = mbuf->buffer) == NULL)
		return NULL;

	if ((source = mbuf->source) != NULL)
		source->rx_credit += mbuf->credit;

	mbuf->buffer = NULL;
	return bp;
}

void
io_endpoint_queue(io_endpoint_list_t *list, io_endpoint_t *ep)
{
	io_endpoint_t **pos, *cur;

	for (pos = &list->head; (cur = *pos) != NULL; pos = &cur->next)
		;
	*pos = ep;
}

static void
io_endpoint_list_purge(io_endpoint_list_t *list)
{
	io_endpoint_t **pos, *cur;

	for (pos = &list->head; (cur = *pos) != NULL; ) {
		if (cur->rfd < 0 && cur->wfd < 0) {
			*pos = cur->next;
			io_endpoint_free(cur);
		} else {
			pos = &cur->next;
		}
	}
}

void
io_hexdump(const io_endpoint_t *sink, const io_mbuf_t *mbuf)
{
	ni_buffer_t *bp = mbuf->buffer;
	unsigned int written, total;

	if (bp == NULL)
		return;

	total =  ni_buffer_count(bp);

	ni_trace(">> Writing %u bytes to socket %d", total, sink->wfd);

	for (written = 0; written < total; ) {
		const unsigned char *p = ni_buffer_head(bp) + written;
		char printed[33];
		unsigned int i, n;

		if ((n = total - written) > 16)
			n = 16;

		for (i = 0; i < n; ++i) {
			unsigned char cc = p[i];

			if (cc < 0x20 || 0x7e < cc)
				cc = '.';

			printed[i] = cc;
		}
		printed[i] = '\0';

		ni_trace("%48s    %s", ni_print_hex(p, n), printed);
		written += n;
	}
}

void
io_endpoint_queue_write(io_endpoint_t *sink, io_mbuf_t *mbuf)
{
	io_endpoint_t *source;
	io_mbuf_t **pos, *cur;

	if (!sink->connected) {
		io_transport_t *xprt = sink->transport;

		ni_debug_socket("sink not yet connected");
		ni_assert(xprt);
		if (!io_transport_connect_finish(xprt, sink))
			ni_fatal("endpoint: delayed connect failed");
	}

	io_hexdump(sink, mbuf);

	if ((source = mbuf->source) != NULL) {
		ni_assert(mbuf->credit <= source->rx_credit);
		source->rx_credit -= mbuf->credit;
	}

	for (pos = &sink->wqueue; (cur = *pos) != NULL; pos = &cur->next)
		;
	*pos = mbuf;
}

static inline ni_bool_t
io_endpoint_is_multiplexing(const io_endpoint_t *ep)
{
	return ep->channel_id == 0;
}

static void
io_endpoint_shutdown(io_endpoint_t *ep, int how)
{
	ni_debug_socket("%s(%u, %s)", __func__, ep->channel_id,
			(how == SHUT_RD)? "SHUT_RD" :
			 (how == SHUT_WR)? "SHUT_WR" :
			  (how == SHUT_RDWR)? "SHUT_RDWR" :
			   "UNKNOWN"
			);
	if (ep->rfd >= 0 && (how == SHUT_RD || how == SHUT_RDWR)) {
		shutdown(ep->rfd, SHUT_RD);
		if (ep->rfd != ep->wfd)
			close(ep->rfd);
		ep->rfd = -1;
	}
	if (ep->wfd >= 0 && (how == SHUT_WR || how == SHUT_RDWR)) {
		shutdown(ep->wfd, SHUT_WR);
		if (ep->rfd != ep->wfd)
			close(ep->wfd);
		ep->wfd = -1;
	}
}

static int
io_endpoint_poll(io_endpoint_t *ep, struct pollfd *pfd, struct pollinfo *pi, io_handler_fn_t handler)
{
	unsigned int nfds = 0;

	if (ep->wfd >= 0 && ep->wbuf == NULL) {
		io_mbuf_t *mbuf = ep->wqueue;

		if (mbuf != NULL) {
			ep->wbuf = io_mbuf_take_buffer(mbuf);
			ep->wqueue = mbuf->next;
			io_mbuf_free(mbuf);
		}

		if (ep->wbuf == NULL && ep->shutdown_write)
			io_endpoint_shutdown(ep, SHUT_WR);
	}

	if (ep->rfd >= 0) {
		if (ep->rx_credit) {
			__set_poll_info(pi, handler, ep);
			pfd->events = POLLIN;
			pfd->fd = ep->rfd;
			nfds++;
			if (ep->rfd == ep->wfd && ep->wbuf) {
				pfd->events |= POLLOUT;
				return nfds;
			}

			++pfd;
			++pi;
		}
	}

	if (ep->wfd >= 0 && ep->wbuf && ni_buffer_count(ep->wbuf)) {
		__set_poll_info(pi, handler, ep);
		pfd->events = POLLOUT;
		pfd->fd = ep->wfd;
		nfds++;
	}

	return nfds;
}

void
io_endpoint_shutdown_source(io_endpoint_t *sink, io_endpoint_t *source)
{
	io_mbuf_t *mbuf;

	if (sink->sink != NULL) {
		ni_assert(sink->sink == source);

		sink->sink = NULL;
		if (sink->wbuf == NULL && sink->wqueue == NULL) {
			io_endpoint_shutdown(sink, SHUT_WR);
		}
	}

	for (mbuf = sink->wqueue; mbuf; mbuf = mbuf->next) {
		if (mbuf->source == source)
			mbuf->source = NULL;
	}
}

static io_endpoint_t *
__io_endpoint_new(int rfd, int wfd, ni_bool_t connected)
{
	io_endpoint_t *ep;

	ep = ni_malloc(sizeof(*ep));
	ep->connected = connected;
	ep->rfd = rfd;
	ep->wfd = wfd;

	return ep;
}

io_endpoint_t *
io_endpoint_pipe_new(io_transport_t *xprt)
{
	io_endpoint_t *ep;

	if (xprt->address) {
		int fd;

		/* Named pipe: */
		if ((fd = open(xprt->address, O_RDWR)) < 0) {
			ni_error("unable to open named pipe \"%s\": %m", xprt->address);
			return NULL;
		}

		ep = __io_endpoint_new(fd, fd, TRUE);
	} else {
		ep = __io_endpoint_new(0, 1, TRUE);
	}

	ep->type = IO_ENDPOINT_TYPE_MULTIPLEX;
	return ep;
}

ni_bool_t
io_endpoint_socket_delayed_connect(io_transport_t *xprt, io_endpoint_t *ep)
{
	struct sockaddr_un sun;

	ni_debug_socket("socket %d: connect to %s", ep->wfd, xprt->address);

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strcpy(sun.sun_path, xprt->address);
	if (connect(ep->wfd, (struct sockaddr *) &sun, SUN_LEN(&sun)) < 0) {
		ni_error("cannot connect PF_LOCAL socket to %s: %m", xprt->address);
		return FALSE;
	}

	fcntl(ep->wfd, F_SETFL, O_NONBLOCK);
	return TRUE;
}

io_endpoint_t *
io_endpoint_socket_new(io_transport_t *xprt)
{
	io_endpoint_t *ep;
	int fd;

	fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (fd < 0)
		ni_fatal("cannot create PF_LOCAL socket: %m");

	fcntl(fd, F_SETFD, FD_CLOEXEC);

	/* Explicitly mark the new endpoint as not yet connected */
	ep = __io_endpoint_new(fd, fd, FALSE);
	ep->transport = xprt;

	/* Note, we do not connect yet - we delay this until later */

	return ep;
}

ni_bool_t
io_endpoint_socket_listen(io_transport_t *xprt)
{
	const char *sockname = xprt->address;

	ni_assert(sockname);
	ni_assert(xprt->listen_fd < 0);

	ni_debug_socket("%s: listening on socket %s", xprt->name, sockname);
	xprt->listen_fd = io_socket_listen(sockname);
	if (xprt->listen_fd < 0)
		return FALSE;

	ni_debug_socket("opened unix listening socket %d", xprt->listen_fd);
	return TRUE;
}

io_endpoint_t *
io_endpoint_socket_accept(io_transport_t *xprt, int listen_fd)
{
	io_endpoint_t *ep;
	static int nfails = 0;
	int fd;

	ni_assert(xprt->listen_fd == listen_fd);
	if ((fd = accept(listen_fd, NULL, NULL)) < 0) {
		ni_error("accept: %m");
		if (nfails > 20)
			ni_fatal("Giving up");
		return NULL;
	}
	nfails = 0;

	ep = __io_endpoint_new(fd, fd, TRUE);

	fcntl(fd, F_SETFL, O_NONBLOCK);
	return ep;
}

/*
 * Handle serial device as endpoint
 */
io_endpoint_t *
io_endpoint_serial_new(io_transport_t *xprt)
{
	io_endpoint_t *ep;
	int fd;

	ni_debug_socket("opening serial device %s", xprt->address);
	fd = open(xprt->address, O_RDWR);
	if (fd < 0)
		ni_fatal("cannot open serial device %s: %m", xprt->address);

	ni_debug_socket("opened serial device %s as fd %d", xprt->address, fd);
	fcntl(fd, F_SETFD, FD_CLOEXEC);

	/* FIXME: set up in raw mode, etc */

	ep = __io_endpoint_new(fd, fd, TRUE);
	return ep;
}

void
io_endpoint_free(io_endpoint_t *ep)
{
	io_endpoint_t *sink;

	ni_debug_dbus("%s(%u)", __func__, ep->channel_id);
	if (ep->wbuf)
		ni_buffer_free(ep->wbuf);
	if (ep->rbuf)
		ni_buffer_free(ep->rbuf);
	while (ep->wqueue != NULL) {
		io_mbuf_t *mbuf = ep->wqueue;

		ep->wqueue = mbuf->next;
		io_mbuf_free(mbuf);
	}

	if ((sink = ep->sink) && sink->sink == ep) {
		/* This is a pair of sockets of the same type. */
		ni_error("%s: forgot to detach sink", __func__);
		io_endpoint_shutdown(sink, SHUT_RDWR);
		sink->sink = NULL;
	}

	free(ep);
}

static io_transport_ops_t	io_unix_transport_ops = {
	.connector		= io_endpoint_socket_new,
	.delayed_connector	= io_endpoint_socket_delayed_connect,
	.acceptor		= io_endpoint_socket_accept,
};

static io_transport_ops_t	io_serial_transport_ops = {
	.connector		= io_endpoint_serial_new,
};

static io_transport_ops_t	io_stdio_transport_ops = {
	.connector		= io_endpoint_pipe_new,
};

static void
__io_transport_init(io_transport_t *xprt, const io_transport_ops_t *ops, const char *address, io_endpoint_type_t type)
{
	xprt->ops = ops;
	xprt->type = type;
	xprt->address = address? ni_strdup(address) : NULL;
	xprt->listen_fd = -1;
}

void
io_transport_unix_init(io_transport_t *xprt, const char *sockname, io_endpoint_type_t type)
{
	ni_debug_socket("%s(%s)", __func__, sockname);
	__io_transport_init(xprt, &io_unix_transport_ops, sockname, type);
}

void
io_transport_serial_init(io_transport_t *xprt, const char *devname)
{
	ni_debug_socket("%s(%s)", __func__, devname);
	__io_transport_init(xprt, &io_unix_transport_ops, devname, IO_ENDPOINT_TYPE_MULTIPLEX);
}

void
io_transport_stdio_init(io_transport_t *xprt)
{
	ni_debug_socket("%s()", __func__);
	__io_transport_init(xprt, &io_stdio_transport_ops, NULL, IO_ENDPOINT_TYPE_MULTIPLEX);
}

static ni_bool_t
io_transport_init(io_transport_t *xprt, const char *param_string)
{
	char *copy, *type, *options;

	type = copy = ni_strdup(param_string);
	if ((options = strchr(type, ':')) != NULL)
		*options++ = '\0';

	if (!strcmp(type, "stdio")) {
		io_transport_stdio_init(xprt);
	} else
	if (!strcmp(type, "unix")) {
		io_transport_unix_init(xprt, options, IO_ENDPOINT_TYPE_SIMPLEX);
	} else
	if (!strcmp(type, "serial")) {
		io_transport_serial_init(xprt, options);
	} else {
		ni_error("%s: cannot parse param string \"%s\"", __func__, param_string);
		ni_error("don't know how to configure %s transport", xprt->name);
		free(copy);
		return FALSE;
	}

	free(copy);

	/* If this is a multiplex transport, open the endpoint right away */
	if (xprt->type == IO_ENDPOINT_TYPE_MULTIPLEX) {
		io_endpoint_t *ep;

		ni_debug_socket("%s: opening multiplexed endpoint now", xprt->name);
		if (!(ep = io_transport_connect(xprt, TRUE))) {
			ni_error("unable to set up %s transport for multiplexing", xprt->name);
			return FALSE;
		}

		xprt->multiplex = ep;
	}

	return TRUE;
}

static io_endpoint_t *
io_transport_connect(io_transport_t *xprt, ni_bool_t now)
{
	io_endpoint_t *ep;

	ni_assert(xprt->ops && xprt->ops->connector);

	if (!(ep = xprt->ops->connector(xprt)))
		return NULL;

	ep->type = xprt->type;

	if (!ep->connected && now && !io_transport_connect_finish(xprt, ep)) {
		io_endpoint_free(ep);
		return NULL;
	}

	return ep;
}

static ni_bool_t
io_transport_connect_finish(io_transport_t *xprt, io_endpoint_t *ep)
{
	if (ep->connected) {
		ni_error("%s: %s endpoint already connected", __func__, xprt->name);
		return FALSE;
	}

	ni_assert(xprt->ops->delayed_connector);
	if (!xprt->ops->delayed_connector(xprt, ep)) {
		ni_error("%s: delayed connect of socket failed", xprt->name);
		return FALSE;
	}

	ep->connected = TRUE;
	return TRUE;
}

static ni_bool_t
io_transport_listen(io_transport_t *xprt)
{
	if (xprt->ops->listen == NULL)
		return TRUE;

	return xprt->ops->listen(xprt);
}

static io_endpoint_t *
io_transport_accept(io_transport_t *xprt, int fd)
{
	io_endpoint_t *ep;

	ep = xprt->ops->acceptor(xprt, fd);
	if (ep == NULL)
		return NULL;

	ep->type = xprt->type;
	return ep;
}

void
proxy_init(struct proxy *proxy)
{
	memset(proxy, 0, sizeof(*proxy));
	proxy->channel_id = 1;

	proxy->upstream.name = "upstream";
	proxy->upstream.other = &proxy->downstream;

	proxy->downstream.name = "downstream";
	proxy->downstream.other = &proxy->upstream;
}

int
io_socket_listen(const char *sockname)
{
	struct sockaddr_un sun;
	socklen_t alen;
	int fd;

	fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (fd < 0)
		ni_fatal("cannot create PF_LOCAL socket: %m");

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strcpy(sun.sun_path, sockname);
	alen = SUN_LEN(&sun);

	unlink(sockname);
	if (bind(fd, (struct sockaddr *) &sun, alen) < 0)
		ni_fatal("cannot bind PF_LOCAL socket to %s: %m", sockname);

	if (listen(fd, 128) < 0)
		ni_fatal("cannot listen on PF_LOCAL socket: %m");

	fcntl(fd, F_SETFD, FD_CLOEXEC);
	return fd;
}

/*
 * Get the size of the next receive buffer
 */
static inline unsigned int
__proxy_recv_bufsiz(const io_endpoint_t *ep)
{
	unsigned int rcount;

	ni_assert(ep->rx_credit);
	if (fcntl(ep->rfd, FIONREAD, &rcount) < 0)
		rcount = 4096;

	if (rcount > ep->rx_credit)
		rcount = ep->rx_credit;
	return rcount;
}

static io_endpoint_t *
__proxy_channel_by_id(const io_transport_t *xprt, unsigned int channel_id)
{
	io_endpoint_t *ep;

	ni_assert(xprt);
	foreach_io_endpoint(ep, &xprt->ep_list) {
		if (ep->channel_id == channel_id)
			return ep;
	}
	return NULL;
}

static const char *
__proxy_cmdname(unsigned int cmd)
{
	static const char *names[__CHANNEL_CMD_COUNT] = {
	[CHANNEL_OPEN]	= "CHANNEL_OPEN",
	[CHANNEL_CLOSE]	= "CHANNEL_CLOSE",
	[CHANNEL_DATA]	= "CHANNEL_DATA",
	};
	const char *n = NULL;

	if (cmd < __CHANNEL_CMD_COUNT)
		n  = names[cmd];
	return n? n : "UNKNOWN";
}

static io_mbuf_t *
__proxy_command_new(unsigned int cmd, unsigned int channel_id, unsigned int count)
{
	struct data_header *hdr;
	ni_buffer_t *bp;

	bp = ni_buffer_new(DATA_HEADER_SIZE + count);
	hdr = ni_buffer_push_tail(bp, DATA_HEADER_SIZE);
	hdr->cmd = htonl(cmd);
	hdr->channel = htonl(channel_id);
	hdr->count = htonl(count);

	return io_mbuf_wrap(bp, NULL);
}

static io_mbuf_t *
proxy_channel_open_new(unsigned int channel_id)
{
	return __proxy_command_new(CHANNEL_OPEN, channel_id, 0);
}

static io_mbuf_t *
proxy_channel_close_new(unsigned int channel_id)
{
	return __proxy_command_new(CHANNEL_CLOSE, channel_id, 0);
}

static void
proxy_demux(io_endpoint_t *source, ni_buffer_t *bp)
{
	const char *myname = source->transport->name;
	io_transport_t *xprt = source->transport->other;
	struct data_header *hdr;
	io_endpoint_t *sink;
	unsigned int channel_id;

	ni_assert(xprt);
	ni_assert(xprt->other == source->transport);

	hdr = ni_buffer_pull_head(bp, DATA_HEADER_SIZE);
	channel_id = ntohl(hdr->channel);

	ni_debug_dbus("%-14s channel %4u count %5u",
			__proxy_cmdname(ntohl(hdr->cmd)), channel_id, ntohl(hdr->count));

	if (hdr->cmd == htonl(CHANNEL_OPEN)) {
		sink = __proxy_channel_by_id(xprt, channel_id);
		if (sink)
			ni_fatal("demux: duplicate open for %s channel %u", xprt->name, channel_id);

		sink = io_transport_connect(xprt, TRUE);
		if (sink == NULL)
			ni_fatal("demux: unable to open %s channel %u: cannot connect to %s",
					xprt->name, channel_id, xprt->address);

		sink->channel_id = channel_id;
		sink->sink = source;
		sink->rx_credit = DEFAULT_CREDIT_SIMPLEX;
		io_endpoint_queue(&xprt->ep_list, sink);

		return;
	}

	sink = __proxy_channel_by_id(xprt, channel_id);
	if (sink == NULL) {
		ni_debug_dbus("demux: dropping %s packet for %s channel %u", myname, xprt->name, channel_id);
		ni_buffer_free(bp);
		return;
	}

	switch (ntohl(hdr->cmd)) {
	case CHANNEL_CLOSE:
		/* Mark the channel for drain and subsequent SHUT_WR */
		ni_buffer_free(bp);
		break;

	case CHANNEL_DATA:
		io_endpoint_queue_write(sink, io_mbuf_wrap(bp, source));
		break;

	default:
		ni_error("unsupported mux packet, cmd=%u", ntohl(hdr->cmd));
		ni_buffer_free(bp);
	}
}

/*
 * We're copying data between two like connections, i.e. either
 * between two simplex DBus connections, or two multiplexed connections.
 * No header munging is necessary, and our endpoint is firmly connected
 * to a specific sink.
 */
static void
proxy_recv_copy(io_endpoint_t *ep)
{
	unsigned int rcount = __proxy_recv_bufsiz(ep);
	ni_buffer_t *bp = NULL;
	int ret;

	ni_assert(ep->sink);

	bp = ni_buffer_new(rcount);

	ret = read(ep->rfd, ni_buffer_tail(bp), ni_buffer_tailroom(bp));
	ni_debug_dbus("%s: read() returns %d", __func__, ret);
	if (ret > 0) {
		ni_buffer_push_tail(bp, ret);
		io_endpoint_queue_write(ep->sink, io_mbuf_wrap(bp, ep));
	} else
	if (ret == 0) {
		/* Inform the sink that we've shut down. */
		ep->sink->shutdown_write = 1;

		/* Shutdown read side of this socket */
		io_endpoint_shutdown(ep, SHUT_RD);
	} else {
		ni_error("read error on socket: %m");
	}
}

/*
 * We're feeding data from a simplex DBus connection
 * into a multiplexed connection.
 * We prepend a DATA header containing the channel ID,
 * then queue it to the data sink endpoint.
 */
static void
proxy_recv_mux(io_endpoint_t *ep)
{
	unsigned int rcount = __proxy_recv_bufsiz(ep);
	io_mbuf_t *mbuf = NULL;
	struct data_header *hdr = NULL;
	ni_buffer_t *bp = NULL;
	int ret;

	ni_assert(ep->sink);
	ni_assert(ep->channel_id != 0);

	bp = ni_buffer_new(rcount + DATA_HEADER_SIZE);
	ni_buffer_reserve_head(bp, DATA_HEADER_SIZE);

	ret = read(ep->rfd, ni_buffer_tail(bp), ni_buffer_tailroom(bp));
	ni_debug_dbus("%s: read() returns %d", __func__, ret);
	if (ret > 0) {
		ni_buffer_push_tail(bp, ret);

		mbuf = io_mbuf_wrap(bp, ep);

		/* This gives us the payload size, not the full packet
		 * size including header. This is important for the rx_credit
		 * calculation, as we want to avoid rx_credit going negative. */
		rcount = ni_buffer_count(bp);

		/* Update the byte count field in the header */
		hdr = (struct data_header *) ni_buffer_push_head(bp, DATA_HEADER_SIZE);
		hdr->cmd = htonl(CHANNEL_DATA);
		hdr->channel = htonl(ep->channel_id);
		hdr->count = htonl(rcount);

		io_endpoint_queue_write(ep->sink, mbuf);
	} else
	if (ret == 0) {
		/* Queue a CLOSE command to the multiplexing connection, to inform
		 * the remote that it can start to tear down this connection */
		io_endpoint_queue_write(ep->sink, proxy_channel_close_new(ep->channel_id));

		/* Inform the sink that we've shut down. */
		//io_endpoint_shutdown_source(ep->sink, ep);

		/* Shutdown read side of this socket */
		io_endpoint_shutdown(ep, SHUT_RD);
	} else {
		ni_error("read error on socket: %m");
	}
}

static void
proxy_recv_demux(io_endpoint_t *ep)
{
	unsigned int rcount = __proxy_recv_bufsiz(ep);
	struct data_header *hdr = NULL;
	ni_buffer_t *bp = NULL;
	ni_bool_t read_some = FALSE;
	int ret;

	if (ep->rbuf == NULL) {
		/* We're at a packet boundary */
		ep->rbuf = ni_buffer_new(4096 + DATA_HEADER_SIZE);
	}

	bp = ep->rbuf;
	while (rcount != 0) {
		unsigned int avail, rnext;

		avail = ni_buffer_count(bp);
		if (avail < DATA_HEADER_SIZE) {
			rnext = DATA_HEADER_SIZE - avail;
		} else {
			unsigned int pktsize;

			hdr = ni_buffer_head(bp);
			pktsize = ntohl(hdr->count) + DATA_HEADER_SIZE;

			if (avail == pktsize) {
				/* We have a full packet */
				proxy_demux(ep, bp);
				ep->rbuf = NULL;
				break;
			}

			ni_assert(avail < pktsize);
			rnext = pktsize - avail;
		}

		if (rnext > rcount)
			rnext = rcount;

		ni_buffer_ensure_tailroom(bp, rnext);

		ret = read(ep->rfd, ni_buffer_tail(bp), rnext);
		ni_debug_dbus("%s: read() returns %d", __func__, ret);
		if (ret > 0) {
			read_some = TRUE;
			rcount -= ret;
			ni_buffer_push_tail(bp, ret);
			continue;
		} else
		if (ret == 0) {
			if (read_some)
				break;
			//io_endpoint_shutdown_source(ep->sink, ep);

			/* Shutdown read side of this socket */
			io_endpoint_shutdown(ep, SHUT_RD);
			break;
		} else {
			ni_error("read error on socket: %m");
			break;
		}
	}
}

static void
proxy_recv(io_endpoint_t *ep)
{
	if (ep->sink && ep->type == ep->sink->type) {
		/* For sockets of the same type, we just copy data as-is */
		proxy_recv_copy(ep);
		return;
	}

	if (ep->type == IO_ENDPOINT_TYPE_SIMPLEX) {
		if (ep->sink == NULL)
			ni_fatal("simplex socket, no peer - seems we shut down already");
		proxy_recv_mux(ep);
	} else {
		proxy_recv_demux(ep);
	}
}

static int
proxy_endpoint_doio(proxy_t *proxy, io_endpoint_t *ep, const struct pollfd *pfd)
{
	int ret;

	if ((ep->rfd == pfd->fd) && (pfd->revents & POLLIN)) {
		ni_debug_dbus("proxy_endpoint_doio: fd %d POLLIN", ep->rfd);
		proxy_recv(ep);
	}

	if ((ep->wfd == pfd->fd) && pfd->revents & POLLOUT) {
		ni_buffer_t *bp = ep->wbuf;

		ret = write(ep->wfd, ni_buffer_head(bp), ni_buffer_count(bp));
		if (ret > 0) {
			ni_buffer_pull_head(bp, ret);
			if (ni_buffer_count(bp) == 0) {
				ni_buffer_free(bp);
				ep->wbuf = NULL;
			}
		} else if (ret < 0) {
			switch (errno) {
			case EPIPE:
				io_endpoint_shutdown(ep, SHUT_WR);
				break;
			default:
				ni_error("iopipe write error: %m");
				break;
			}
		}
	}

	return 0;
}

static char *	proxy_sockname = NULL;
static int	proxy_done = 0;

void
reaper(int sig)
{
	/* For now, the proxy will just terminate gracelessly.
	 * In an ideal world, we should drain our buffers if we have any.
	 */
	proxy_done = 1;
}

void
proxy_cleanup(void)
{
	if (proxy_sockname)
		unlink(proxy_sockname);
}

int
do_exec(char **argv)
{
	static char sockname[PATH_MAX];
	int listen_fd;
	pid_t pid;

	snprintf(sockname, sizeof(sockname), "/tmp/dbus-proxy.%d", getpid());
	if ((listen_fd = io_socket_listen(sockname)) < 0)
		return -1;

	proxy_sockname = sockname;
	atexit(proxy_cleanup);

	{
		struct sigaction act;
		sigset_t set;

		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);

		memset(&act, 0, sizeof(act));
		//act.sa_flags = SA_NOCLDSTOP;
		act.sa_handler = reaper;
		sigaction(SIGCHLD, &act, NULL);

		ni_trace("Installed sighandler for sigchld");
	}

	pid = fork();
	if (pid < 0)
		ni_fatal("unable to fork: %m");
	if (pid == 0) {
		setenv("DBUS_SESSION_BUS_ADDRESS", sockname, 1);

		execv(argv[0], argv);
		ni_fatal("unable to exec %s: %m", argv[0]);
	}

	return listen_fd;
}

static int
proxy_downstream_accept(proxy_t *proxy, io_endpoint_t *dummy, const struct pollfd *pfd)
{
	if (pfd->revents & POLLIN) {
		io_endpoint_t *ep, *upstream;

		ep = io_transport_accept(&proxy->downstream, pfd->fd);
		if (ep == NULL)
			return 0;

		ep->channel_id = proxy->channel_id++;
		ep->rx_credit = DEFAULT_CREDIT_SIMPLEX;
		ni_assert(ep->channel_id);

		if ((upstream = proxy->upstream.multiplex) != NULL) {
			/* Send a CHANNEL_OPEN command to upstream */
			io_endpoint_queue_write(upstream, proxy_channel_open_new(ep->channel_id));
		} else {
			/* We create an upstream end point, but do not connect it immediately.
			 * With KVM, for instance, we may get a connection from KVM, but data
			 * from the agent running in the guest will not arrive for a long time.
			 * This is usually too long for dbus-daemon, which will simply disconnect
			 * us after 5 seconds. */
			upstream = io_transport_connect(&proxy->upstream, FALSE);
			if (upstream == NULL) {
				io_endpoint_free(ep);
				return 0;
			}
			upstream->channel_id = proxy->channel_id++;
			upstream->rx_credit = DEFAULT_CREDIT_SIMPLEX;
			upstream->sink = ep;

			io_endpoint_queue(&proxy->upstream.ep_list, upstream);
		}

		io_endpoint_queue(&proxy->downstream.ep_list, ep);
		ep->sink = upstream;
	}

	return 0;
}

void
do_proxy(proxy_t *proxy)
{
	static const unsigned int MAXCONN = 128;

	struct pollinfo info[MAXCONN + 1];
	struct pollfd pfd[MAXCONN + 1];

	//signal(SIGPIPE, SIG_IGN);

	if (!opt_foreground) {
		if (ni_server_background(program_name) < 0)
			ni_fatal("unable to background server");
	}

	while (!proxy_done) {
		io_endpoint_t *ep;
		unsigned int nfds = 0;
		int n;

		if (proxy->downstream.listen_fd >= 0) {
			/* FIXME: could use io_endpoint_poll as well */
			__set_poll_info(info + nfds, proxy_downstream_accept, NULL);
			pfd[nfds].fd = proxy->downstream.listen_fd;
			pfd[nfds].events = POLLIN;
			nfds++;
		}

		foreach_io_endpoint(ep, &proxy->upstream.ep_list)
			nfds += io_endpoint_poll(ep, pfd + nfds, info + nfds, proxy_endpoint_doio);

		foreach_io_endpoint(ep, &proxy->downstream.ep_list)
			nfds += io_endpoint_poll(ep, pfd + nfds, info + nfds, proxy_endpoint_doio);

		n = poll(pfd, nfds, 100000);
		if (n < 0) {
			if (errno != EINTR)
				ni_error("poll: %m");
			continue;
		}

		for (n = 0; n < nfds; ++n)
			info[n].handler(proxy, info[n].ep, pfd + n);

		io_endpoint_list_purge(&proxy->upstream.ep_list);
		io_endpoint_list_purge(&proxy->downstream.ep_list);

#if 0
		if (proxy->upstream.multiplex && io_endpoint_can_send(proxy->upstream.ep_list)) {
			io_endpoint_t *ep;

			/* Select next work item.
			 * a) If downstream opened a new connection, open a new channel with upstream
			 * b) Pick the next downstream socket with data that wants to be sent.
			 */
			if ((ep = io_endpoint_dequeue(&proxy->upstream.ep_list_embryonic)) != NULL) {
				ni_buffer_t *cmd;

				cmd = proxy_command_new(PROXY_OPEN, ep->channel_id);
				io_endpoint_send(proxy->upstream.ep_list, cmd);
				io_endpoint_queue(&proxy->downstream.ep_list, ep);
			} else {
				io_endpoint_t *head, *ep;

				head = proxy->downstream.next_sender;
				if (head == NULL)
					head = proxy->downstream.ep_list;

				ep = head;
				do {
					if (ep->rbuf && ni_buffer_count(ep->rbuf)) {
						proxy->downstream.next_sender = ep->next;
						break;
					}
					ep = ep->next;
				} while (ep != head);
			}

		}
#endif
	}

	ni_trace("Child exited, proxy done");
}
