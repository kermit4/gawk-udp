/*
 * udp.c -- GAWK extension providing unconnected UDP socket functions.
 *
 * Functions exported to GAWK:
 *   udp_open(port)                         -> fd  (or -1 on error)
 *   udp_sendto(fd, data, host, port)       -> bytes sent  (or -1)
 *   udp_recvfrom(fd, result [, timeout_ms])-> bytes received  (0 = timeout, -1 = error)
 *       fills result["data"], result["host"], result["port"]
 *   udp_close(fd)                          -> 0
 *
 * Build:  see Makefile
 * Load:   @load "udp"  (or -l udp) in your GAWK script
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>   /* required before gawkapi.h -- it uses struct stat */
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "gawkapi.h"

int plugin_is_GPL_compatible;  /* required by gawk -- declares GPL compatibility */

static const gawk_api_t *api;
static awk_ext_id_t ext_id;
static const char *ext_version = "udp extension: version 1.0";
static awk_bool_t (*init_func)(void) = NULL;

/* ------------------------------------------------------------------ */

/*
 * udp_open(port) -> fd
 * Creates a SOCK_DGRAM socket and binds it to 0.0.0.0:port.
 * Pass port=0 to let the OS pick an ephemeral port (send-only use).
 */
static awk_value_t *
do_udp_open(int nargs, awk_value_t *result, struct awk_ext_func *finfo)
{
    awk_value_t port_val;
    struct sockaddr_in addr;
    int fd, one = 1;
    (void)nargs; (void)finfo;

    if (!get_argument(0, AWK_NUMBER, &port_val)) {
        warning(ext_id, "udp_open: missing port argument");
        return make_number(-1, result);
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        warning(ext_id, "udp_open: socket: %s", strerror(errno));
        return make_number(-1, result);
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)(int)port_val.num_value);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        warning(ext_id, "udp_open: bind port %d: %s",
                (int)port_val.num_value, strerror(errno));
        close(fd);
        return make_number(-1, result);
    }

    return make_number(fd, result);
}

/* ------------------------------------------------------------------ */

/*
 * udp_sendto(fd, data, host, port) -> bytes_sent
 * Resolves host (name or dotted-decimal) and sends data as a single
 * UDP datagram.
 */
static awk_value_t *
do_udp_sendto(int nargs, awk_value_t *result, struct awk_ext_func *finfo)
{
    awk_value_t fd_val, data_val, host_val, port_val;
    struct addrinfo hints, *res0;
    char port_str[16];
    ssize_t sent;
    int fd, rc;
    (void)nargs; (void)finfo;

    if (!get_argument(0, AWK_NUMBER, &fd_val)  ||
        !get_argument(1, AWK_STRING, &data_val) ||
        !get_argument(2, AWK_STRING, &host_val) ||
        !get_argument(3, AWK_NUMBER, &port_val)) {
        warning(ext_id, "udp_sendto: requires 4 arguments: fd, data, host, port");
        return make_number(-1, result);
    }

    fd = (int)fd_val.num_value;
    snprintf(port_str, sizeof(port_str), "%d", (int)port_val.num_value);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    rc = getaddrinfo(host_val.str_value.str, port_str, &hints, &res0);
    if (rc != 0) {
        warning(ext_id, "udp_sendto: getaddrinfo %s: %s",
                host_val.str_value.str, gai_strerror(rc));
        return make_number(-1, result);
    }

    sent = sendto(fd,
                  data_val.str_value.str, data_val.str_value.len,
                  0,
                  res0->ai_addr, res0->ai_addrlen);
    freeaddrinfo(res0);

    if (sent < 0)
        warning(ext_id, "udp_sendto: sendto: %s", strerror(errno));

    return make_number((double)sent, result);
}

/* ------------------------------------------------------------------ */

/*
 * udp_recvfrom(fd, result [, timeout_ms]) -> bytes_received
 *
 * Waits up to timeout_ms milliseconds for a datagram (default: block).
 * Returns:
 *    > 0   datagram received; result["data"], result["host"], result["port"] set
 *      0   timeout elapsed with no data
 *    < 0   error
 *
 * result["port"] is set as a string so callers can use it directly in
 * "host:port" concatenation.
 */
static awk_value_t *
do_udp_recvfrom(int nargs, awk_value_t *result, struct awk_ext_func *finfo)
{
    awk_value_t fd_val, arr_param, timeout_val;
    int fd;
    long timeout_ms = -1;
    (void)finfo;
    struct timeval tv;
    fd_set rfds;
    ssize_t n;
    struct sockaddr_in src;
    socklen_t srclen = sizeof(src);
    static char buf[65536];
    char host_str[INET_ADDRSTRLEN];
    char port_str[16];
    awk_array_t arr;
    awk_value_t idx, val;
    char *copy;

    if (!get_argument(0, AWK_NUMBER, &fd_val)) {
        warning(ext_id, "udp_recvfrom: missing fd argument");
        return make_number(-1, result);
    }
    if (!get_argument(1, AWK_ARRAY, &arr_param)) {
        warning(ext_id, "udp_recvfrom: second argument must be an array");
        return make_number(-1, result);
    }
    if (nargs >= 3 && get_argument(2, AWK_NUMBER, &timeout_val))
        timeout_ms = (long)timeout_val.num_value;

    fd  = (int)fd_val.num_value;
    arr = arr_param.array_cookie;

    if (timeout_ms >= 0) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000L;
        int r = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (r == 0)  return make_number(0, result);
        if (r < 0) { warning(ext_id, "udp_recvfrom: select: %s", strerror(errno));
                     return make_number(-1, result); }
    }

    n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                 (struct sockaddr *)&src, &srclen);
    if (n < 0) {
        warning(ext_id, "udp_recvfrom: recvfrom: %s", strerror(errno));
        return make_number(-1, result);
    }
    buf[n] = '\0';

    inet_ntop(AF_INET, &src.sin_addr, host_str, sizeof(host_str));
    snprintf(port_str, sizeof(port_str), "%d", (int)ntohs(src.sin_port));

    /* result["data"] */
    make_const_string("data", 4, &idx);
    copy = gawk_malloc(n + 1);
    memcpy(copy, buf, n);
    copy[n] = '\0';
    make_malloced_string(copy, (size_t)n, &val);
    set_array_element(arr, &idx, &val);

    /* result["host"] */
    make_const_string("host", 4, &idx);
    {
        size_t hlen = strlen(host_str);
        char *h = gawk_malloc(hlen + 1);
        memcpy(h, host_str, hlen + 1);
        make_malloced_string(h, hlen, &val);
    }
    set_array_element(arr, &idx, &val);

    /* result["port"] */
    make_const_string("port", 4, &idx);
    {
        size_t plen = strlen(port_str);
        char *p = gawk_malloc(plen + 1);
        memcpy(p, port_str, plen + 1);
        make_malloced_string(p, plen, &val);
    }
    set_array_element(arr, &idx, &val);

    return make_number((double)n, result);
}

/* ------------------------------------------------------------------ */

/*
 * udp_close(fd) -> 0
 */
static awk_value_t *
do_udp_close(int nargs, awk_value_t *result, struct awk_ext_func *finfo)
{
    awk_value_t fd_val;
    (void)nargs; (void)finfo;
    if (!get_argument(0, AWK_NUMBER, &fd_val)) {
        warning(ext_id, "udp_close: missing fd argument");
        return make_number(-1, result);
    }
    close((int)fd_val.num_value);
    return make_number(0, result);
}

/* ------------------------------------------------------------------ */

static awk_ext_func_t func_table[] = {
    { "udp_open",     do_udp_open,     1, 1, awk_false, NULL },
    { "udp_sendto",   do_udp_sendto,   4, 4, awk_false, NULL },
    { "udp_recvfrom", do_udp_recvfrom, 3, 2, awk_false, NULL },
    { "udp_close",    do_udp_close,    1, 1, awk_false, NULL },
};

dl_load_func(func_table, udp, "")
