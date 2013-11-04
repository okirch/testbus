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
 *  -	Run the agent in a KVM guest
 *
 *	On the host, run:
 *	 dbus-proxy --downstream unix-mux:/var/run/testbus-guest.sock
 *
 *	Then, start KVM using a virtio-serial port connecting to the
 *	downstream socket:
 *
 *	 kvm ... \
 *	        -device virtio-serial \
 *	        -device virtserialport,chardev=testbus-serial,name=org.opensuse.Testbus.0 \
 *	        -chardev socket,id=testbus-serial,path=/var/run/testbus-guest.sock \
 *		...
 *
 *	Finally, inside the guest, run another proxy, like this:
 *
 *	 dbus-proxy --upstream serial:/dev/virtio-ports/org.opensuse.Testbus.0 \
 *		 --downstream unix:/var/run/dbus-proxy.sock
 *
 *	Now, the testbus client (or any other DBus Client) can connect to the host DBus
 *	daemon if you point it to the proxy socket, either by setting the environment
 *	variable DBUS_SESSION_BUS_ADDRESS:
 *
 *	 export DBUS_SESSION_BUS_ADDRESS=unix:path=/var/run/dbus-proxy.sock
 *
 *	OR by adding the following to /etc/testbus/config.xml (in the guest)
 *
 *	 <dbus socket="/var/run/dbus-proxy.sock" />
 *
 *	Following either of these, you can start the testbus agent and/or run
 *	testbus client commands.
 *
 *  -	Run the agent in a KVM or XEN guest
 *
 *	[To be fleshed out, probably very similar to KVM case]
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
	OPT_IDENTITY,
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
	{ "identity",		required_argument,	NULL,	OPT_IDENTITY },

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
	io_endpoint_t **prevp;
	io_endpoint_t *	next;

	io_endpoint_type_t type;
	io_transport_t *transport;
	char *		name;		/* for debugging */

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

#define CHANNEL_ID_NONE			0

#define DEFAULT_CREDIT_SIMPLEX		8192
#define DEFAULT_CREDIT_MULTIPLEX	(16 * DEFAULT_CREDIT_SIMPLEX)

struct io_endpoint_list {
	io_endpoint_t *	head;
};

#define foreach_io_endpoint(ep, list) \
	for (ep = (list)->head; ep; ep = ep->next)

struct io_transport_ops {
	io_endpoint_t *	(*connector)(io_transport_t *, unsigned int channel_id);
	io_endpoint_t *	(*acceptor)(io_transport_t *, unsigned int channel_id, int fd);
	ni_bool_t	(*delayed_connector)(io_transport_t *, io_endpoint_t *);
	ni_bool_t	(*listen)(io_transport_t *);
};

struct io_transport {
	const char *	name;
	const io_transport_ops_t *ops;

	void		(*data_available)(io_endpoint_t *);

	io_endpoint_type_t type;
	io_endpoint_t *	multiplex;
	io_endpoint_list_t ep_list;
	io_endpoint_list_t garbage_list;

	const char *	address;
	int		listen_fd;

	io_transport_t *other;

	uint32_t	next_channel_id;
};

typedef struct proxy	proxy_t;
struct proxy {
	io_transport_t	upstream;
	io_transport_t	downstream;
};

static const char *	program_name;
static const char *	opt_identity;
static const char *	opt_log_target;
static int		opt_foreground;
static char *		opt_upstream = "unix:/var/run/dbus/system_bus_socket";
static char *		opt_downstream;

static void		proxy_init(proxy_t *);
static void		proxy_setup_recv(proxy_t *);
static void		do_proxy(proxy_t *);

static io_mbuf_t *	io_mbuf_wrap(ni_buffer_t *, io_endpoint_t *);
static void		io_mbuf_free(io_mbuf_t *);
static ni_buffer_t *	io_mbuf_take_buffer(io_mbuf_t *);

static int		io_socket_listen(const char *);

static void		io_endpoint_queue_write(io_endpoint_t *, io_mbuf_t *);
static void		io_endpoint_write_queue_discard(io_endpoint_t *);
static void		io_endpoint_link(io_endpoint_t *, io_endpoint_list_t *);
static void		io_endpoint_unlink(io_endpoint_t *);

static ni_bool_t	io_endpoint_socket_listen(io_transport_t *xprt);
static io_endpoint_t *	io_endpoint_socket_accept(io_transport_t *xprt, unsigned int channel_id, int fd);
static void		io_endpoint_free(io_endpoint_t *);
static const char *	io_endpoint_type_name(io_endpoint_type_t);

static ni_bool_t	io_transport_init(io_transport_t *xprt, const char *param_string, ni_bool_t active);
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

		case OPT_IDENTITY:
			opt_identity = optarg;
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

	if (ni_init("proxy") < 0)
		return 1;

	if (opt_log_target == NULL) {
		ni_log_destination_default(program_name, opt_foreground);
	} else
	if (!ni_log_destination(program_name, opt_log_target)) {
		fprintf(stderr, "Bad log destination \%s\"\n", opt_log_target);
		return 1;
	}

	proxy_init(&proxy);

	if (opt_upstream == NULL)
		opt_upstream = "unix:/var/run/dbus/system_bus_socket";

	if (!io_transport_init(&proxy.upstream, opt_upstream, TRUE))
		ni_fatal("Unable to set up upstream transport");

	if (opt_downstream == NULL) {
#if 0
  		proxy.downstream.listen_fd = do_exec(opt_argv);
  		proxy.downstream.acceptor = proxy_accept;
#else
		ni_trace("exec not supported right now");
#endif
	} else {
		if (!io_transport_init(&proxy.downstream, opt_downstream, FALSE))
			ni_fatal("Unable to set up downstream transport");
	}

	/* Now set up transport.data_available for copy/mux/demux on both
	 * transports, so that we do not have to make the same comparisons
	 * each time we have a POLLIN event.
	 */
	proxy_setup_recv(&proxy);

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

static inline void
io_set_poll_info(struct pollfd *pfd, int fd, int events, struct pollinfo *pi, io_handler_fn_t handler, io_endpoint_t *ep)
{
	pfd->events = events | POLLHUP;
	pfd->fd = fd;

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
io_endpoint_link(io_endpoint_t *ep, io_endpoint_list_t *list)
{
	io_endpoint_t **pos, *cur;

	for (pos = &list->head; (cur = *pos) != NULL; pos = &cur->next)
		;
	*pos = ep;
	ep->prevp = pos;
}

void
io_endpoint_unlink(io_endpoint_t *ep)
{
	io_transport_t *xprt = ep->transport;
	io_endpoint_t **pos;

	/* FIXME: when setting an endpoint up as a multiplexing connnecting,
	 * ep->prevp should point back to xprt->multiplex, too */
	if (xprt->multiplex == ep) {
		xprt->multiplex = NULL;
		return;
	}

	ni_assert(ep->prevp);
	pos = ep->prevp;
	*pos = ep->next;
	if (ep->next)
		ep->next->prevp = pos;
	ep->prevp = NULL;
	ep->next = NULL;
}

static unsigned int
__io_endpoint_list_purge(io_endpoint_t **pos)
{
	io_endpoint_t *cur;
	unsigned int freed = 0;

	while ((cur = *pos) != NULL) {
		if (cur->rfd < 0 && cur->wfd < 0) {
			*pos = cur->next;
			io_endpoint_free(cur);
			freed++;
		} else {
			pos = &cur->next;
		}
	}

	return freed;
}

static unsigned int
io_endpoint_list_purge(io_endpoint_list_t *list)
{
	return __io_endpoint_list_purge(&list->head);
}

void
io_hexdump(const io_endpoint_t *sink, const io_mbuf_t *mbuf)
{
	ni_buffer_t *bp = mbuf->buffer;
	unsigned int written, total;

	if (bp == NULL)
		return;

	total =  ni_buffer_count(bp);

	ni_debug_socket("%s: queuing %u bytes", sink->name, total);

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

		ni_trace("%-47s    %s", ni_print_hex(p, n), printed);
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

ni_buffer_t *
io_endpoint_pullup(io_endpoint_t *ep)
{
	if (ep->wbuf != NULL) {
		ni_assert(ni_buffer_count(ep->wbuf));
		return ep->wbuf;
	}

	if (ep->wbuf == NULL) {
		io_mbuf_t *mbuf = ep->wqueue;

		if (mbuf != NULL) {
			ep->wbuf = io_mbuf_take_buffer(mbuf);
			ep->wqueue = mbuf->next;
			io_mbuf_free(mbuf);
			ni_debug_socket("%s: grabbed next buffer from wqueue", ep->name);
			return ep->wbuf;
		}
	}

	return NULL;
}

static const char *
io_endpoint_type_name(io_endpoint_type_t t)
{
	switch (t) {
	case IO_ENDPOINT_TYPE_NONE:
		return "none";
	case IO_ENDPOINT_TYPE_SIMPLEX:
		return "simplex";
	case IO_ENDPOINT_TYPE_MULTIPLEX:
		return "multiplex";
	default:
		return "unknown";
	}
}

static inline ni_bool_t
io_endpoint_is_multiplexing(const io_endpoint_t *ep)
{
	return ep->channel_id == 0;
}

static void
io_endpoint_shutdown(io_endpoint_t *ep, int how)
{
	ni_debug_socket("%s: shutdown(%s)", ep->name,
			(how == SHUT_RD)? "SHUT_RD" :
			 (how == SHUT_WR)? "SHUT_WR" :
			  (how == SHUT_RDWR)? "SHUT_RDWR" :
			   "UNKNOWN"
			);
	if (ep->rfd >= 0 && (how == SHUT_RD || how == SHUT_RDWR)) {
		shutdown(ep->rfd, SHUT_RD);
		if (ep->rfd != ep->wfd) {
			ni_debug_socket("%s: closing fd %d", ep->name, ep->rfd);
			close(ep->rfd);
		}
		ep->rfd = -1;
	}
	if (ep->wfd >= 0 && (how == SHUT_WR || how == SHUT_RDWR)) {
		if (io_endpoint_pullup(ep)) {
			ni_debug_socket("%s: write shutdown, discard pending writes", ep->name);
			io_endpoint_write_queue_discard(ep);
		}

		shutdown(ep->wfd, SHUT_WR);
		if (ep->rfd != ep->wfd) {
			ni_debug_socket("%s: closing fd %d", ep->name, ep->wfd);
			close(ep->wfd);
		}
		ep->wfd = -1;
	}

	if (ep->rfd < 0 && ep->wfd < 0) {
		io_transport_t *xprt = ep->transport;

		ni_debug_socket("%s: socket is dead, moving to garbage list", ep->name);
		io_endpoint_unlink(ep);
		io_endpoint_link(ep, &xprt->garbage_list);
	}
}

/*
 * Half-close an endpoint.
 * This is called when detect an EOF on the corresponding channel or endpoint
 * (e.g. after recv returns 0 bytes, or when we receive a CHANNEL_CLOSE on a
 * multiplexed connection).
 * In this case, the end point can no longer recv anything, and should be marked
 * for future shutdown after draining all pending writes.
 */
static void
io_endpoint_halfclose(io_endpoint_t *ep)
{
	int how = SHUT_RD;

	ep->shutdown_write = TRUE;
	if (!io_endpoint_pullup(ep))
		how = SHUT_RDWR;

	io_endpoint_shutdown(ep, how);
}

static int
io_endpoint_doio(proxy_t *proxy, io_endpoint_t *ep, const struct pollfd *pfd)
{
	int ret;

	if (pfd->revents == 0)
		return 0;

#if 0
	ni_debug_socket("%s: doio%s%s", ep->name,
				(pfd->revents & POLLIN)? " POLLIN" : "",
				(pfd->revents & POLLOUT)? " POLLOUT" : "");
#endif

	if ((ep->rfd == pfd->fd) && (pfd->revents & POLLIN)) {
		ep->transport->data_available(ep);
	}

	if ((ep->wfd == pfd->fd) && pfd->revents & POLLOUT) {
		ni_buffer_t *bp = ep->wbuf;

		ret = write(ep->wfd, ni_buffer_head(bp), ni_buffer_count(bp));
		if (ret > 0) {
			ni_debug_socket("%s: transmitted %u bytes", ep->name, ret);
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
				ni_error("%s: write error: %m", ep->name);
				break;
			}
		}
	}

	if ((ep->wfd == pfd->fd) && pfd->revents & POLLHUP) {
		io_endpoint_shutdown(ep, SHUT_WR);
	}

	return 0;
}

static void
io_endpoint_poll_prepare(io_endpoint_t *ep)
{
	if (ep->wfd >= 0 && !io_endpoint_pullup(ep)) {
		if (ep->shutdown_write)
			io_endpoint_shutdown(ep, SHUT_WR);
	}
}

static int
io_endpoint_poll(io_endpoint_t *ep, struct pollfd *pfd, struct pollinfo *pi, io_handler_fn_t handler)
{
	int nfds = 0;

	if (ep->rfd < 0 && ep->wfd < 0)
		return 0;

	if (!ep->connected) {
		ni_debug_socket("%s: not yet connected, no POLL", ep->name);
		return 0;
	}

	if (ep->wbuf)
		ni_assert(ni_buffer_count(ep->wbuf) != 0);

	if (ep->rfd >= 0 && !ep->rx_credit)
		ni_trace("%s: no rx credits", ep->name);

	if (ep->rfd == ep->wfd) {
		int events = 0;
		if (ep->rx_credit)
			events |= POLLIN;
		if (ep->wbuf)
			events |= POLLOUT;
		if (events == 0)
			return 0;

		io_set_poll_info(pfd, ep->rfd, events, pi, handler, ep);
		return 1;
	}

	if (ep->rfd >= 0 && ep->rx_credit) {
		io_set_poll_info(&pfd[nfds], ep->rfd, POLLIN, &pi[nfds], handler, ep);
		nfds++;
	}

	if (ep->wfd >= 0 && ep->wbuf && ni_buffer_count(ep->wbuf)) {
		io_set_poll_info(&pfd[nfds], ep->rfd, POLLOUT, &pi[nfds], handler, ep);
		nfds++;
	}

	return nfds;
}

static io_endpoint_t *
__io_endpoint_new(io_transport_t *xprt, unsigned int channel_id, int rfd, int wfd, ni_bool_t connected)
{
	char namebuf[128];
	io_endpoint_t *ep;

	ep = ni_malloc(sizeof(*ep));
	ep->transport = xprt;
	ep->type = xprt->type;
	ep->channel_id = channel_id;
	ep->connected = connected;
	ep->rfd = rfd;
	ep->wfd = wfd;

	if (xprt->type == IO_ENDPOINT_TYPE_SIMPLEX)
		ep->rx_credit = DEFAULT_CREDIT_SIMPLEX;
	else
		ep->rx_credit = DEFAULT_CREDIT_MULTIPLEX;

	/* This should never be an issue, but better be safe than sorry */
	if (channel_id > xprt->next_channel_id)
		xprt->next_channel_id = channel_id + 1;

	snprintf(namebuf, sizeof(namebuf), "%s%u", xprt->name, channel_id);
	ep->name = ni_strdup(namebuf);

	return ep;
}

io_endpoint_t *
io_endpoint_pipe_new(io_transport_t *xprt, unsigned int channel_id)
{
	io_endpoint_t *ep;

	if (xprt->address) {
		int fd;

		/* Named pipe: */
		if ((fd = open(xprt->address, O_RDWR)) < 0) {
			ni_error("unable to open named pipe \"%s\": %m", xprt->address);
			return NULL;
		}

		ep = __io_endpoint_new(xprt, channel_id, fd, fd, TRUE);
	} else {
		ep = __io_endpoint_new(xprt, channel_id, 0, 1, TRUE);
	}

	ep->type = IO_ENDPOINT_TYPE_MULTIPLEX;
	return ep;
}

ni_bool_t
io_endpoint_socket_delayed_connect(io_transport_t *xprt, io_endpoint_t *ep)
{
	struct sockaddr_un sun;

	ni_debug_socket("%s: connecting socket fd %d to %s", ep->name, ep->wfd, xprt->address);

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strcpy(sun.sun_path, xprt->address);
	if (connect(ep->wfd, (struct sockaddr *) &sun, SUN_LEN(&sun)) < 0) {
		ni_error("%s: cannot connect PF_LOCAL socket to %s: %m", xprt->name, xprt->address);
		return FALSE;
	}

	fcntl(ep->wfd, F_SETFL, O_NONBLOCK);
	ep->connected = TRUE;
	return TRUE;
}

io_endpoint_t *
io_endpoint_socket_new(io_transport_t *xprt, unsigned int channel_id)
{
	io_endpoint_t *ep;
	int fd;

	fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (fd < 0)
		ni_fatal("cannot create PF_LOCAL socket: %m");

	fcntl(fd, F_SETFD, FD_CLOEXEC);

	/* Explicitly mark the new endpoint as not yet connected.
	 * This happens when we actually queue data to this socket.
	 */
	ep = __io_endpoint_new(xprt, channel_id, fd, fd, FALSE);

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
io_endpoint_socket_accept(io_transport_t *xprt, unsigned int channel_id, int listen_fd)
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

	ni_debug_socket("%s: accepted incoming connection on fd %d", xprt->name, fd);

	ep = __io_endpoint_new(xprt, channel_id, fd, fd, TRUE);

	fcntl(fd, F_SETFL, O_NONBLOCK);
	return ep;
}

/*
 * Handle serial device as endpoint
 */
io_endpoint_t *
io_endpoint_serial_new(io_transport_t *xprt, unsigned int channel_id)
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

	ep = __io_endpoint_new(xprt, channel_id, fd, fd, TRUE);
	return ep;
}

/*
 * Socket destruction functions
 */
void
io_endpoint_write_queue_discard(io_endpoint_t *ep)
{
	if (ep->wbuf) {
		ni_buffer_free(ep->wbuf);
		ep->wbuf = NULL;
	}

	while (ep->wqueue != NULL) {
		io_mbuf_t *mbuf = ep->wqueue;

		ep->wqueue = mbuf->next;
		io_mbuf_free(mbuf);
	}
}

void
io_endpoint_free(io_endpoint_t *ep)
{
	io_endpoint_t *sink;

	ni_debug_dbus("%s(%s)", __func__, ep->name);
	if (ep->rbuf) {
		ni_buffer_free(ep->rbuf);
		ep->rbuf = NULL;
	}

	io_endpoint_write_queue_discard(ep);

	if ((sink = ep->sink) && sink->sink == ep) {
		/* This is a pair of sockets of the same type. */
		ni_error("%s: forgot to detach sink", __func__);
		io_endpoint_shutdown(sink, SHUT_RDWR);
		sink->sink = NULL;
	}

	ni_string_free(&ep->name);

	free(ep);
}

static io_transport_ops_t	io_unix_transport_ops = {
	.connector		= io_endpoint_socket_new,
	.delayed_connector	= io_endpoint_socket_delayed_connect,
	.acceptor		= io_endpoint_socket_accept,
	.listen			= io_endpoint_socket_listen,
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
	__io_transport_init(xprt, &io_unix_transport_ops, sockname, type);
}

void
io_transport_serial_init(io_transport_t *xprt, const char *devname)
{
	__io_transport_init(xprt, &io_serial_transport_ops, devname, IO_ENDPOINT_TYPE_MULTIPLEX);
}

void
io_transport_stdio_init(io_transport_t *xprt)
{
	__io_transport_init(xprt, &io_stdio_transport_ops, NULL, IO_ENDPOINT_TYPE_MULTIPLEX);
}

static ni_bool_t
io_transport_init(io_transport_t *xprt, const char *param_string, ni_bool_t active)
{
	char *copy, *type, *options;

	type = copy = ni_strdup(param_string);
	if ((options = strchr(type, ':')) != NULL)
		*options++ = '\0';

	ni_debug_socket("%s: init transport type=%s, options=%s)", xprt->name, type, options);
	if (!strcmp(type, "stdio")) {
		io_transport_stdio_init(xprt);
	} else
	if (!strcmp(type, "unix")) {
		io_transport_unix_init(xprt, options, IO_ENDPOINT_TYPE_SIMPLEX);
	} else
	if (!strcmp(type, "unix-mux")) {
		io_transport_unix_init(xprt, options, IO_ENDPOINT_TYPE_MULTIPLEX);
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

	if (!active) {
		if (!io_transport_listen(xprt))
			ni_fatal("failed to open %s endpoint for listening", xprt->name);
	}

	/* If this is a multiplex transport, open the endpoint right away */
	if (xprt->type == IO_ENDPOINT_TYPE_MULTIPLEX && active) {
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
__io_transport_connect(io_transport_t *xprt, unsigned int channel_id, ni_bool_t now)
{
	io_endpoint_t *ep;

	ni_assert(xprt->ops && xprt->ops->connector);
	if (!(ep = xprt->ops->connector(xprt, channel_id)))
		return NULL;

	ep->type = xprt->type;

	if (!ep->connected && now && !io_transport_connect_finish(xprt, ep)) {
		io_endpoint_free(ep);
		return NULL;
	}

	return ep;
}

static io_endpoint_t *
io_transport_connect(io_transport_t *xprt, ni_bool_t now)
{
	uint32_t channel_id = CHANNEL_ID_NONE;

	return __io_transport_connect(xprt, channel_id, now);
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
	uint32_t channel_id = CHANNEL_ID_NONE;
	io_endpoint_t *ep;

	if (xprt->type == IO_ENDPOINT_TYPE_SIMPLEX)
		channel_id = xprt->next_channel_id++;

	ep = xprt->ops->acceptor(xprt, channel_id, fd);
	if (ep == NULL)
		return NULL;

	ep->connected = TRUE;
	ep->type = xprt->type;
	return ep;
}

unsigned int
io_transport_purge(io_transport_t *xprt)
{
	return io_endpoint_list_purge(&xprt->ep_list)
	     + io_endpoint_list_purge(&xprt->garbage_list)
	     + __io_endpoint_list_purge(&xprt->multiplex);
}

/*
 * Before polling, loop over all endpoints.
 *  1.	If there's data on the write queue, make sure we always have
 *	a buffer in ep->wbuf.
 *	If the socket has been marked for write shutdown and the
 *	write queue is empty, shut it down now.
 *	If the socket's read side has already been shut down, it
 *	will be closed completely.
 *
 *  2.	Purge dead sockets.
 */
static void
io_transport_poll_prepare(io_transport_t *xprt)
{
	io_endpoint_t *ep;

	if ((ep = xprt->multiplex) != NULL) {
		io_endpoint_poll_prepare(ep);
		__io_endpoint_list_purge(&xprt->multiplex);
	}

	foreach_io_endpoint(ep, &xprt->ep_list)
		io_endpoint_poll_prepare(ep);
	io_endpoint_list_purge(&xprt->ep_list);
}

static unsigned int
io_transport_poll(io_transport_t *xprt, struct pollfd *pfd, struct pollinfo *pi)
{
	unsigned int nfds = 0;
	io_endpoint_t *ep;

	io_transport_poll_prepare(xprt);
	io_transport_purge(xprt);

	if ((ep = xprt->multiplex) != NULL)
		nfds += io_endpoint_poll(ep, pfd + nfds, pi + nfds, io_endpoint_doio);

	foreach_io_endpoint(ep, &xprt->ep_list)
		nfds += io_endpoint_poll(ep, pfd + nfds, pi + nfds, io_endpoint_doio);

	return nfds;
}

void
proxy_init(struct proxy *proxy)
{
	memset(proxy, 0, sizeof(*proxy));

	proxy->upstream.name = "upstream";
	proxy->upstream.other = &proxy->downstream;
	proxy->upstream.next_channel_id = 1;

	proxy->downstream.name = "downstream";
	proxy->downstream.other = &proxy->upstream;
	proxy->downstream.next_channel_id = 1;
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
proxy_demux_packet(io_endpoint_t *source, ni_buffer_t *bp)
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

	ni_debug_wicked("%s: %s channel %4u count %5u",
			source->name, __proxy_cmdname(ntohl(hdr->cmd)), channel_id, ntohl(hdr->count));

	if (hdr->cmd == htonl(CHANNEL_OPEN)) {
		sink = __proxy_channel_by_id(xprt, channel_id);
		if (sink) {
			ni_error("demux: duplicate open for %s channel %u", xprt->name, channel_id);
		}

		sink = __io_transport_connect(xprt, channel_id, TRUE);
		if (sink == NULL)
			ni_fatal("demux: unable to open %s channel %u: cannot connect to %s",
					xprt->name, channel_id, xprt->address);

		sink->channel_id = channel_id;
		sink->sink = source;
		sink->rx_credit = DEFAULT_CREDIT_SIMPLEX;
		io_endpoint_link(sink, &xprt->ep_list);

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
		ni_buffer_free(bp);

		/* After draining all pending output, we should close this socket */
		io_endpoint_halfclose(sink);
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
	if (ret > 0) {
		ni_debug_socket("%s: read %d bytes", ep->name, ret);
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
		ni_debug_socket("%s: read 0 bytes, starting to shut down channel %u", ep->name, ep->channel_id);

		/* Queue a CLOSE command to the multiplexing connection, to inform
		 * the remote that it can start to tear down this connection */
		io_endpoint_queue_write(ep->sink, proxy_channel_close_new(ep->channel_id));

		/* Shutdown read side of this socket */
		io_endpoint_shutdown(ep, SHUT_RD);
	} else {
		ni_error("%s: read error on socket: %m", ep->name);
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
				ni_debug_socket("%s: received packet of %u bytes", ep->name, avail);
				proxy_demux_packet(ep, bp);
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
		if (ret > 0) {
			read_some = TRUE;
			rcount -= ret;
			ni_buffer_push_tail(bp, ret);
			continue;
		} else
		if (ret == 0) {
			if (read_some) {
				ni_debug_socket("%s: received partial packet (%u bytes)", ep->name, avail);
				break;
			}

			ni_debug_socket("%s: connection closed, should close all endpoints", ep->name);
			{
				io_transport_t *xprt = ep->transport->other;
				io_endpoint_t *other;

				foreach_io_endpoint(other, &xprt->ep_list) {
					io_endpoint_halfclose(other);
				}

			}

			/* Shutdown read side of this socket */
			io_endpoint_shutdown(ep, SHUT_RD);
			break;
		} else {
			ni_error("%s: read error on socket: %m", ep->name);
			break;
		}
	}
}

void
proxy_setup_recv(proxy_t *proxy)
{
	io_transport_t *upstream = &proxy->upstream;
	io_transport_t *downstream = &proxy->downstream;

	if (upstream->type == downstream->type) {
		upstream->data_available = 
		downstream->data_available = proxy_recv_copy;
	} else
	if (upstream->type == IO_ENDPOINT_TYPE_SIMPLEX
	 && downstream->type == IO_ENDPOINT_TYPE_MULTIPLEX) {
		upstream->data_available = proxy_recv_mux;
		downstream->data_available = proxy_recv_demux;
	} else
	if (upstream->type == IO_ENDPOINT_TYPE_MULTIPLEX
	 && downstream->type == IO_ENDPOINT_TYPE_SIMPLEX) {
		upstream->data_available = proxy_recv_demux;
		downstream->data_available = proxy_recv_mux;
	} else {
		ni_fatal("cannot set up proxy: unknown transport combination - upstram %s, downstream %s",
				io_endpoint_type_name(upstream->type),
				io_endpoint_type_name(downstream->type));
	}
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

#ifdef currently_not_used
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
#endif

static int
proxy_downstream_accept(proxy_t *proxy, io_endpoint_t *dummy, const struct pollfd *pfd)
{
	io_transport_t *xprt = &proxy->downstream;

	if (pfd->revents & POLLIN) {
		io_endpoint_t *ep, *upstream;

		ep = io_transport_accept(xprt, pfd->fd);
		if (ep == NULL)
			return 0;

		if (ep->type == IO_ENDPOINT_TYPE_MULTIPLEX) {
			/* We accepted a multiplex connection. */
			if (xprt->multiplex != NULL)
				ni_fatal("Currently not supported: more than one multiplex connection");

			xprt->multiplex = ep;
		} else {
			io_transport_t *other = xprt->other;

			if ((upstream = other->multiplex) != NULL) {
				/* Send a CHANNEL_OPEN command to upstream */
				io_endpoint_queue_write(upstream, proxy_channel_open_new(ep->channel_id));
			} else {
				/* We create an upstream end point, but do not connect it immediately.
				 * With KVM, for instance, we may get a connection from KVM, but data
				 * from the agent running in the guest will not arrive for a long time.
				 * This is usually too long for dbus-daemon, which will simply disconnect
				 * us after 5 seconds. */
				upstream = io_transport_connect(other, FALSE);
				if (upstream == NULL) {
					io_endpoint_free(ep);
					return 0;
				}
				upstream->sink = ep;

				/* TBD: set up upstream->recv handler */
				io_endpoint_link(upstream, &other->ep_list);
			}

			io_endpoint_link(ep, &xprt->ep_list);
			ep->sink = upstream;
		}

		/* TBD: set up ep->recv handler */
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
		if (ni_server_background(opt_identity? opt_identity : program_name) < 0)
			ni_fatal("unable to background server");
	}

	while (!proxy_done) {
		unsigned int nfds = 0;
		int n;

		if (proxy->downstream.listen_fd >= 0) {
			/* FIXME: could use io_endpoint_poll as well */
			__set_poll_info(info + nfds, proxy_downstream_accept, NULL);
			pfd[nfds].fd = proxy->downstream.listen_fd;
			pfd[nfds].events = POLLIN;
			nfds++;
		}

		nfds += io_transport_poll(&proxy->upstream, pfd + nfds, info + nfds);
		nfds += io_transport_poll(&proxy->downstream, pfd + nfds, info + nfds);

#if 1
		{
			unsigned int i;

			ni_debug_socket("polling %u sockets", nfds);
			for (i = 0; i < nfds; ++i) {
				io_endpoint_t *ep = info[i].ep;
				unsigned int events = pfd[i].events;

				ni_debug_socket("%-12s %s%s",
							ep? ep->name : "nil",
							(events & POLLIN)? " POLLIN" : "",
							(events & POLLOUT)? " POLLOUT" : "");
			}
		}
#endif

		n = poll(pfd, nfds, 100000);
		if (n < 0) {
			if (errno != EINTR)
				ni_error("poll: %m");
			continue;
		}

		if (n != 0)
			ni_debug_socket("%u poll events", n);

		for (n = 0; n < nfds; ++n)
			info[n].handler(proxy, info[n].ep, pfd + n);
	}

	ni_debug_socket("Child exited, proxy done");
}
