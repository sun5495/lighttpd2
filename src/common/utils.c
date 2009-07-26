
#ifdef LIGHTY_OS_NETBSD
	#define _NETBSD_SOURCE
#endif

#ifdef LIGHTY_OS_OPENBSD
	#warning OpenBSD does net allow sending of file descriptors when _XOPEN_SOURCE is defined (needed for Solaris)
#else
	#define _XOPEN_SOURCE
	#define _XOPEN_SOURCE_EXTENDED 1
#endif

#include <lighttpd/utils.h>
#include <lighttpd/ip_parsers.h>

#include <stdio.h>
#include <fcntl.h>

#if 0
#include <stropts.h>
#endif

/* for send/li_receive_fd */
union fdmsg {
  struct cmsghdr h;
  gchar buf[1000];
};


void li_fatal(const gchar* msg) {
	fprintf(stderr, "%s\n", msg);
	abort();
}

void li_fd_no_block(int fd) {
#ifdef O_NONBLOCK
	fcntl(fd, F_SETFL, O_NONBLOCK | O_RDWR);
#elif defined _WIN32
	int i = 1;
	ioctlsocket(fd, FIONBIO, &i);
#else
#error No way found to set non-blocking mode for fds.
#endif
}

void li_fd_block(int fd) {
#ifdef O_NONBLOCK
	fcntl(fd, F_SETFL, O_RDWR);
#elif defined _WIN32
	int i = 0;
	ioctlsocket(fd, FIONBIO, &i);
#else
#error No way found to set blocking mode for fds.
#endif
}

void li_fd_init(int fd) {
#ifdef FD_CLOEXEC
	/* close fd on exec (cgi) */
	fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
	li_fd_no_block(fd);
}

#if 0
#ifndef _WIN32
int li_send_fd(int s, int fd) { /* write fd to unix socket s */
	for ( ;; ) {
		if (-1 == ioctl(s, I_SENDFD, fd)) {
			switch (errno) {
			case EINTR: break;
			case EAGAIN: return -2;
			default: return -1;
			}
		} else {
			return 0;
		}
	}
}

int li_receive_fd(int s, int *fd) { /* read fd from unix socket s */
	struct strrecvfd res;
	memset(&res, 0, sizeof(res));
	for ( ;; ) {
		if (-1 == ioctl(s, I_RECVFD, &res)) {
			switch (errno) {
			case EINTR: break;
			case EAGAIN: return -2;
			default: return -1;
			}
		} else {
			*fd = res.fd;
		}
	}
}
#endif
#endif

gint li_send_fd(gint s, gint fd) { /* write fd to unix socket s */
	struct msghdr msg;
	struct iovec  iov;
#ifdef CMSG_FIRSTHDR
	struct cmsghdr *cmsg;
#ifndef CMSG_SPACE
#define CMSG_SPACE(x) x+100
#endif
	gchar buf[CMSG_SPACE(sizeof(gint))];
#endif

	iov.iov_len = 1;
	iov.iov_base = "x";
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = 0;
	msg.msg_namelen = 0;
#ifdef CMSG_FIRSTHDR
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
#ifndef CMSG_LEN
#define CMSG_LEN(x) x
#endif
	cmsg->cmsg_len = CMSG_LEN(sizeof(gint));
	msg.msg_controllen = cmsg->cmsg_len;
	*((gint*)CMSG_DATA(cmsg)) = fd;
#else
	msg.msg_accrights = (gchar*)&fd;
	msg.msg_accrightslen = sizeof(fd);
#endif

	for (;;) {
		if (sendmsg(s, &msg, 0) < 0) {
			switch (errno) {
			case EINTR: continue;
#if EAGAIN != EWOULDBLOCK
			case EWOULDBLOCK:
#endif
			case EAGAIN: return -2;
			default: return -1;
			}
		}

		break;
	}

	return 0;
}


gint li_receive_fd(gint s, gint *fd) { /* read fd from unix socket s */
	struct iovec iov;
	struct msghdr msg;
#ifdef CMSG_FIRSTHDR
	union fdmsg cmsg;
	struct cmsghdr* h;
#else
	gint _fd;
#endif
	gchar x[100];
	gchar name[100];

	iov.iov_base = x;
	iov.iov_len = 100;
	msg.msg_name = name;
	msg.msg_namelen = 100;
#ifdef CMSG_FIRSTHDR
	msg.msg_control = cmsg.buf;
	msg.msg_controllen = sizeof(union fdmsg);
#else
	msg.msg_accrights = (gchar*)&_fd;
	msg.msg_accrightslen = sizeof(_fd);
#endif
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
#ifdef CMSG_FIRSTHDR
	msg.msg_flags = 0;
	h = CMSG_FIRSTHDR(&msg);
#ifndef CMSG_LEN
	#define CMSG_LEN(x) x
#endif
	h->cmsg_len = CMSG_LEN(sizeof(gint));
	h->cmsg_level = SOL_SOCKET;
	h->cmsg_type = SCM_RIGHTS;
	*((gint*)CMSG_DATA(h)) = -1;
#endif

	for (;;) {
		if (recvmsg(s, &msg, 0) == -1) {
			switch (errno) {
			case EINTR: continue;
#if EAGAIN != EWOULDBLOCK
			case EWOULDBLOCK:
#endif
			case EAGAIN: return -2;
			default: return -1;
			}
		}

		break;
	}

#ifdef CMSG_FIRSTHDR
	h = CMSG_FIRSTHDR(&msg);

	if (!h || h->cmsg_len != CMSG_LEN(sizeof(gint)) || h->cmsg_level != SOL_SOCKET || h->cmsg_type != SCM_RIGHTS) {
	#ifdef EPROTO
		errno = EPROTO;
	#else
		errno = EINVAL;
	#endif
		return -1;
	}

	*fd = *((gint*)CMSG_DATA(h));

	return 0;
#else
	if (msg.msg_accrightslen != sizeof(fd))
		return -1;

	*fd = _fd;

	return 0;
#endif
}



void li_ev_io_add_events(struct ev_loop *loop, ev_io *watcher, int events) {
	if ((watcher->events & events) == events) return;
	ev_io_stop(loop, watcher);
	ev_io_set(watcher, watcher->fd, watcher->events | events);
	ev_io_start(loop, watcher);
}

void li_ev_io_rem_events(struct ev_loop *loop, ev_io *watcher, int events) {
	if (0 == (watcher->events & events)) return;
	ev_io_stop(loop, watcher);
	ev_io_set(watcher, watcher->fd, watcher->events & ~events);
	ev_io_start(loop, watcher);
}

void li_ev_io_set_events(struct ev_loop *loop, ev_io *watcher, int events) {
	if (events == (watcher->events & (EV_READ | EV_WRITE))) return;
	ev_io_stop(loop, watcher);
	ev_io_set(watcher, watcher->fd, (watcher->events & ~(EV_READ | EV_WRITE)) | events);
	ev_io_start(loop, watcher);
}


/* converts hex char (0-9, A-Z, a-z) to decimal.
 * returns -1 on invalid input.
 */
static int hex2int(unsigned char hex) {
	int res;
	if (hex >= 'A') { /* 'A' < 'a': hex >= 'A' --> hex >= 'a' */
		if (hex >= 'a') {
			res = hex - 'a' + 10;
		} else {
			res = hex - 'A' + 10;
		}
	} else {
		res = hex - '0';
	}
	if (res > 15)
		res = -1;

	return res;
}

void li_url_decode(GString *path) {
	char *src, *dst, c;
	src = dst = path->str;
	for ( ; *src; src++) {
		c = *src;
		if (c == '%') {
			if (src[1] && src[2]) {
				int a = hex2int(src[1]), b = hex2int(src[2]);
				if (a != -1 && b != -1) {
					c = (a << 4) | b;
					if (c < 32 || c == 127) c = '_';
					*(dst++) = c;
				}
				src += 2;
			} else {
				/* end of string */
				return;
			}
		} else {
			if (c < 32 || c == 127) c = '_';
			*(dst++) = c;
		}
	}
	g_string_set_size(path, dst - path->str);
}

/* Remove "/../", "//", "/./" parts from path.
 *
 * /blah/..         gets  /
 * /blah/../foo     gets  /foo
 * /abc/./xyz       gets  /abc/xyz
 * /abc//xyz        gets  /abc/xyz
 *
 * NOTE: src and dest can point to the same buffer, in which case
 *       the operation is performed in-place.
 */

void li_path_simplify(GString *path) {
	int toklen;
	char c, pre1;
	char *start, *slash, *walk, *out;
	unsigned short pre;

	if (path == NULL)
		return;

	walk  = start = out = slash = path->str;
	while (*walk == ' ') {
		walk++;
	}

	pre1 = *(walk++);
	c    = *(walk++);
	pre  = pre1;
	if (pre1 != '/') {
		pre = ('/' << 8) | pre1;
		*(out++) = '/';
	}
	*(out++) = pre1;

	if (pre1 == '\0') {
		g_string_set_size(path, out - start);
		return;
	}

	while (1) {
		if (c == '/' || c == '\0') {
			toklen = out - slash;
			if (toklen == 3 && pre == (('.' << 8) | '.')) {
				out = slash;
				if (out > start) {
					out--;
					while (out > start && *out != '/') {
						out--;
					}
				}

				if (c == '\0')
					out++;
			} else if (toklen == 1 || pre == (('/' << 8) | '.')) {
				out = slash;
				if (c == '\0')
					out++;
			}

			slash = out;
		}

		if (c == '\0')
			break;

		pre1 = c;
		pre  = (pre << 8) | pre1;
		c    = *walk;
		*out = pre1;

		out++;
		walk++;
	}

	g_string_set_size(path, out - start);
}


GString *li_counter_format(guint64 count, liCounterType t, GString *dest) {
	guint64 rest;

	if (!dest)
		dest = g_string_sized_new(10);
	else
		g_string_truncate(dest, 0);

	switch (t) {
	case COUNTER_TIME:
		/* 123 days 12 hours 32 min 5 s */
		if (count > (3600*24)) {
			g_string_append_printf(dest, "%"G_GUINT64_FORMAT" days", count / (3600*24));
			count %= (3600*24);
		}
		if (count > 3600) {
			g_string_append_printf(dest, "%s%"G_GUINT64_FORMAT" hours", dest->len ? " ":"", count / 3600);
			count %= 3600;
		}
		if (count > 60) {
			g_string_append_printf(dest, "%s%"G_GUINT64_FORMAT" min", dest->len ? " ":"", count / 60);
			count %= 60;
		}
		if (count || !dest->len) {
			g_string_append_printf(dest, "%s%"G_GUINT64_FORMAT" s", dest->len ? " ":"", count);
		}
		break;
	case COUNTER_BYTES:
		/* B KB MB GB TB PB */
		if (count >> 50) {
			/* PB */
			rest = (((count >> 40) & 1023) * 100) / 1024;
			g_string_append_printf(dest, "%"G_GUINT64_FORMAT".%02"G_GUINT64_FORMAT" PB", count >> 50, rest);
		} else if (count >> 40) {
			/* TB */
			rest = (((count >> 30) & 1023) * 100) / 1024;
			g_string_append_printf(dest, "%"G_GUINT64_FORMAT".%02"G_GUINT64_FORMAT" TB", count >> 40, rest);
		} else if (count >> 30) {
			/* GB */
			rest = (((count >> 20) & 1023) * 100) / 1024;
			g_string_append_printf(dest, "%"G_GUINT64_FORMAT".%02"G_GUINT64_FORMAT" GB", count >> 30, rest);
		} else if (count >> 20) {
			/* MB */
			rest = (((count >> 10) & 1023) * 100) / 1024;
			g_string_append_printf(dest, "%"G_GUINT64_FORMAT".%02"G_GUINT64_FORMAT" MB", count >> 20, rest);
		} else if (count >> 10) {
			/* KB */
			rest = ((count & 1023) * 100) / 1024;
			g_string_append_printf(dest, "%"G_GUINT64_FORMAT".%02"G_GUINT64_FORMAT" KB", count >> 10, rest);
		} else {
			/* B */
			g_string_append_printf(dest, "%"G_GUINT64_FORMAT" B", count);
		}
		break;
	case COUNTER_UNITS:
		/* m k */
		if (count < 1000) {
			g_string_append_printf(dest, "%"G_GUINT64_FORMAT, count);
		} else if (count < 1000*1000) {
			g_string_append_printf(dest, "%"G_GUINT64_FORMAT".%02"G_GUINT64_FORMAT" k", count / (guint64)1000, (count % (guint64)1000) / 10);
		} else {
			g_string_append_printf(dest, "%"G_GUINT64_FORMAT".%02"G_GUINT64_FORMAT" m", count / (guint64)(1000*1000), (count % (guint64)(1000*1000)) / 10000);
		}
		break;
	}

	return dest;
}


gchar *li_ev_backend_string(guint backend) {
	switch (backend) {
		case EVBACKEND_SELECT:	return "select";
		case EVBACKEND_POLL:	return "poll";
		case EVBACKEND_EPOLL:	return "epoll";
		case EVBACKEND_KQUEUE:	return "kqueue";
		case EVBACKEND_DEVPOLL:	return "devpoll";
		case EVBACKEND_PORT:	return "port";
		default:				return "unknown";
	}
}


void li_string_destroy_notify(gpointer str) {
	g_string_free((GString*)str, TRUE);
}


guint li_hash_ipv4(gconstpointer key) {
	return *((guint*)key) * 2654435761;
}

guint li_hash_ipv6(gconstpointer key) {
	guint *i = ((guint*)key);
	return (i[0] ^ i[1] ^ i[2] ^ i[3]) * 2654435761;
}


GString *li_sockaddr_to_string(liSocketAddress addr, GString *dest, gboolean showport) {
	gchar *p;
	guint8 len = 0;
	guint8 tmp;
	guint8 tmplen;
	guint8 oct;
	liSockAddr *saddr = addr.addr;

	switch (saddr->plain.sa_family) {
	case AF_INET:
		/* ipv4 */
		if (!dest)
			dest = g_string_sized_new(16+6);
		else
			g_string_set_size(dest, 16+6);

		p = dest->str;

		for (guint i = 0; i < 4; i++) {
			oct = ((guint8*)&saddr->ipv4.sin_addr.s_addr)[i];
			for (tmplen = 1, tmp = oct; tmp > 9; tmp /= 10)
				tmplen++;

			len += tmplen + 1;
			tmp = tmplen;

			p[tmplen] = '.';

			for (p += tmplen-1; tmplen; tmplen--) {
				*p = '0' + (oct % 10);
				p--;
				oct /= 10;
			}

			p += tmp + 2;
		}

		dest->str[len-1] = 0;
		dest->len = len-1;
		if (showport) g_string_append_printf(dest, ":%u", (unsigned int) ntohs(saddr->ipv4.sin_port));
		break;
#ifdef HAVE_IPV6
	case AF_INET6:
		/* ipv6 - not yet implemented with own function */
		if (!dest)
			dest = g_string_sized_new(INET6_ADDRSTRLEN+6);

		li_ipv6_tostring(dest, saddr->ipv6.sin6_addr.s6_addr);
		if (showport) g_string_append_printf(dest, ":%u", (unsigned int) ntohs(saddr->ipv6.sin6_port));
		break;
#endif
#ifdef HAVE_SYS_UN_H
	case AF_UNIX:
		if (!dest)
			dest = g_string_sized_new(0);
		else
			g_string_truncate(dest, 0);
		g_string_append_len(dest, CONST_STR_LEN("unix:"));
		g_string_append(dest, saddr->un.sun_path);
		break;
#endif
	default:
		if (!dest)
			dest = g_string_new_len(CONST_STR_LEN("unknown sockaddr family"));
		else
			li_string_assign_len(dest, CONST_STR_LEN("unknown sockaddr family"));
	}

	return dest;
}

liSocketAddress li_sockaddr_from_string(GString *str, guint tcp_default_port) {
	guint32 ipv4;
#ifdef HAVE_IPV6
	guint8 ipv6[16];
#endif
	guint16 port = tcp_default_port;
	liSocketAddress saddr = { 0, NULL };

#ifdef HAVE_SYS_UN_H
	if (0 == strncmp(str->str, "unix:/", 6)) {
		saddr.len = str->len + 1 - 5 + sizeof(saddr.addr->un.sun_family);
		saddr.addr = (liSockAddr*) g_slice_alloc0(saddr.len);
		saddr.addr->un.sun_family = AF_UNIX;
		strcpy(saddr.addr->un.sun_path, str->str + 5);
	} else
#endif
	if (li_parse_ipv4(str->str, &ipv4, NULL, &port)) {
		if (!port) return saddr;
		saddr.len = sizeof(struct sockaddr_in);
		saddr.addr = (liSockAddr*) g_slice_alloc0(saddr.len);
		saddr.addr->ipv4.sin_family = AF_INET;
		saddr.addr->ipv4.sin_addr.s_addr = ipv4;
		saddr.addr->ipv4.sin_port = htons(port);
#ifdef HAVE_IPV6
	} else
	if (li_parse_ipv6(str->str, ipv6, NULL, &port)) {
		if (!port) return saddr;
		saddr.len = sizeof(struct sockaddr_in6);
		saddr.addr = (liSockAddr*) g_slice_alloc0(saddr.len);
		saddr.addr->ipv6.sin6_family = AF_INET6;
		memcpy(&saddr.addr->ipv6.sin6_addr, ipv6, 16);
		saddr.addr->ipv6.sin6_port = htons(port);
#endif
	}
	return saddr;
}

liSocketAddress li_sockaddr_local_from_socket(gint fd) {
	socklen_t l = 0;
	static struct sockaddr sa;
	liSocketAddress saddr = { 0, NULL };

	if (-1 == getsockname(fd, &sa, &l)) {
		return saddr;
	}

	saddr.addr = (liSockAddr*) g_slice_alloc0(l);
	saddr.len = l;
	getsockname(fd, (struct sockaddr*) saddr.addr, &l);

	return saddr;
}

liSocketAddress li_sockaddr_remote_from_socket(gint fd) {
	socklen_t l = 0;
	static struct sockaddr sa;
	liSocketAddress saddr = { 0, NULL };

	if (-1 == getpeername(fd, &sa, &l)) {
		return saddr;
	}

	saddr.addr = (liSockAddr*) g_slice_alloc0(l);
	saddr.len = l;
	getpeername(fd, (struct sockaddr*) saddr.addr, &l);

	return saddr;
}

void li_sockaddr_clear(liSocketAddress *saddr) {
	if (saddr->addr) g_slice_free1(saddr->len, saddr->addr);
	saddr->addr = NULL;
	saddr->len = 0;
}

/* unused */
void li_gstring_replace_char_with_str_len(GString *gstr, gchar c, gchar *str, guint len) {
	for (guint i = 0; i < gstr->len; i++) {
		if (gstr->str[i] == c) {
			/* char found, replace */
			gstr->str[i] = str[0];
			if (len > 1)
				g_string_insert_len(gstr, i, &str[1], len-1);
			i += len - 1;
		}
	}
}

gboolean li_strncase_equal(GString *str, const gchar *s, guint len) {
	if (str->len != len) return FALSE;
	return 0 == g_ascii_strncasecmp(str->str, s, len);
}

gboolean li_string_suffix(GString *str, const gchar *s, gsize len) {
	if (str->len < len)
		return FALSE;

	return (strcmp(str->str + str->len - len, s) == 0);
}

gboolean li_string_prefix(GString *str, const gchar *s, gsize len) {
	if (str->len < len)
		return FALSE;

	return (strncmp(str->str, s, len) == 0);
}

GString *li_string_assign_len(GString *string, const gchar *val, gssize len) {
	g_string_truncate(string, 0);
	g_string_append_len(string, val, len);
	return string;
}

void li_string_append_int(GString *dest, gint64 v) {
	gchar *buf, *end, swap;
	gsize len;
	guint64 val;

	len = dest->len + 1;
	g_string_set_size(dest, dest->len + 21);
	buf = dest->str + len - 1;

	if (v < 0) {
		len++;
		*(buf++) = '-';
		/* -v maybe < 0 for signed types, so just cast it to unsigned to get the correct value */
		val = -v;
	} else {
		val = v;
	}

	end = buf;
	while (val > 9) {
		*(end++) = '0' + (val % 10);
		val = val / 10;
	}
	*(end) = '0' + val;
	*(end + 1) = '\0';
	len += end - buf;

	while (buf < end) {
		swap = *end;
		*end = *buf;
		*buf = swap;

		buf++;
		end--;
	}

	dest->len = len;
}

/* http://womble.decadentplace.org.uk/readdir_r-advisory.html */
gsize li_dirent_buf_size(DIR * dirp) {
	glong name_max;
 	gsize name_end;

#	if !defined(HAVE_DIRFD)
		UNUSED(dirp);
#	endif

#	if defined(HAVE_FPATHCONF) && defined(HAVE_DIRFD) && defined(_PC_NAME_MAX)
		name_max = fpathconf(dirfd(dirp), _PC_NAME_MAX);
		if (name_max == -1)
#			if defined(NAME_MAX)
				name_max = (NAME_MAX > 255) ? NAME_MAX : 255;
#			else
				return (gsize)(-1);
#			endif
#	else
#		if defined(NAME_MAX)
			name_max = (NAME_MAX > 255) ? NAME_MAX : 255;
#		else
#			error "buffer size for readdir_r cannot be determined"
#		endif
#	endif

    name_end = (gsize)offsetof(struct dirent, d_name) + name_max + 1;

    return (name_end > sizeof(struct dirent) ? name_end : sizeof(struct dirent));
}

const char *li_remove_path(const char *path) {
	char *p = strrchr(path, DIR_SEPERATOR);
	if (NULL != p && *(p) != '\0') {
		return (p + 1);
	}
	return path;
}

GQuark li_sys_error_quark() {
	return g_quark_from_static_string("li-sys-error-quark");
}

gboolean _li_set_sys_error(GError **error, const gchar *msg, const gchar *file, int lineno) {
	int code = errno;
	g_set_error(error, LI_SYS_ERROR, code, "(%s:%d): %s: %s", file, lineno, msg, g_strerror(code));
	return FALSE;
}