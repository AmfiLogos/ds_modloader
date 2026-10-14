/* Framed byte-stream link, fd transport, blob fragments, logging. */
#include "dsnet_internal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

#define MAX_FRAME (DSNET_MAX_PAYLOAD + 64)

static void (*log_fn)(const char *);
static int log_env = -1;
void dsnet_set_log(void (*fn)(const char *line)) { log_fn = fn; }
void dsnet_logf(const char *fmt, ...) {
    if (!log_fn) {
        if (log_env < 0) { const char *e = getenv("DSNET_LOG"); log_env = e && *e == '1'; }
        if (!log_env) return;
    }
    char line[512];
    va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    if (log_fn) log_fn(line); else fprintf(stderr, "[dsnet] %s\n", line);
}

char     dsnet_build_id[32];
uint32_t dsnet_mod_version;
uint8_t  dsnet_uid[16];
int      dsnet_uid_set;
void dsnet_set_player_uid(const uint8_t uid[16]) {
    dsnet_uid_set = uid != NULL;
    if (uid) memcpy(dsnet_uid, uid, 16);
}
void dsnet_set_identity(const char *build_id, uint32_t mod_version) {
    snprintf(dsnet_build_id, sizeof dsnet_build_id, "%s", build_id ? build_id : "");
    dsnet_mod_version = mod_version;
}

static int rx_timeout = -1;
int dsnet_rx_timeout_ms(void) {
    if (rx_timeout < 0) { const char *e = getenv("DSNET_RX_TIMEOUT_MS"); rx_timeout = e && *e ? atoi(e) : 5000; if (rx_timeout < 0) rx_timeout = 0; }
    return rx_timeout;
}
void dsnet_set_rx_timeout_ms(int ms) { rx_timeout = ms < 0 ? 0 : ms; }

double dsnet_now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ---- fd transport ---- */
static long fd_write(void *t, const void *buf, size_t n) {
    ssize_t r = send((int)(intptr_t)t, buf, n, 0);
    if (r >= 0) return r;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
}
static long fd_read(void *t, void *buf, size_t n) {
    ssize_t r = recv((int)(intptr_t)t, buf, n, 0);
    if (r > 0) return r;
    if (r == 0) return -1;
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
}
static int  fd_poll(void *t) { return (int)(intptr_t)t; }
static void fd_close(void *t) { close((int)(intptr_t)t); }
dsnet_transport dsnet_fd_transport(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    return (dsnet_transport){ fd_write, fd_read, fd_poll, fd_close, (void *)(intptr_t)fd };
}

/* ---- link ---- */
void dsnet_link_init(dsnet_link *l, dsnet_transport tr) {
    memset(l, 0, sizeof *l);
    l->tr = tr;
    pthread_mutex_init(&l->mu, NULL);
}
void dsnet_link_free(dsnet_link *l) {
    if (l->tr.close) l->tr.close(l->tr.t);
    free(l->out); free(l->in);
    pthread_mutex_destroy(&l->mu);
}

static int reserve(uint8_t **buf, size_t *off, size_t *len, size_t *cap, size_t need) {
    if (*off && *off == *len) *off = *len = 0;
    if (*len + need <= *cap) return 0;
    if (*off) { memmove(*buf, *buf + *off, *len - *off); *len -= *off; *off = 0; }
    if (*len + need <= *cap) return 0;
    size_t nc = *cap ? *cap : 65536;
    while (nc < *len + need) nc *= 2;
    uint8_t *nb = realloc(*buf, nc);
    if (!nb) return -1;
    *buf = nb; *cap = nc;
    return 0;
}

static int append_frame_locked(dsnet_link *l, const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen) {
    if (reserve(&l->out, &l->out_off, &l->out_len, &l->out_cap, 4 + alen + blen)) return -1;
    uint8_t *p = put32(l->out + l->out_len, alen + blen);
    memcpy(p, a, alen);
    if (blen) memcpy(p + alen, b, blen);
    l->out_len += 4 + alen + blen;
    return 0;
}

int dsnet_link_send(dsnet_link *l, const uint8_t *body, uint32_t len) {
    pthread_mutex_lock(&l->mu);
    int r = l->dead ? -1 : append_frame_locked(l, body, len, NULL, 0);
    pthread_mutex_unlock(&l->mu);
    return r;
}

int dsnet_link_send_blob(dsnet_link *l, uint32_t id, uint8_t kind, uint32_t tick, uint32_t seq_after,
                         const uint8_t *data, uint32_t len) {
    if (len > DSNET_MAX_BLOB) return -1;
    pthread_mutex_lock(&l->mu);
    int r = l->dead ? -1 : 0;
    uint32_t off = 0;
    do {   /* an empty blob still sends one fragment */
        uint32_t n = len - off < DSNET_FRAG ? len - off : DSNET_FRAG;
        uint8_t h[1 + BLOB_HDR], *p = h;
        p = put8(p, P_BLOB); p = put32(p, id); p = put8(p, kind); p = put32(p, len);
        p = put32(p, off); p = put32(p, tick); p = put32(p, seq_after);
        if (!r) r = append_frame_locked(l, h, sizeof h, data + off, n);
        off += n;
    } while (off < len && !r);
    pthread_mutex_unlock(&l->mu);
    return r;
}

int dsnet_link_has_out(dsnet_link *l) {
    pthread_mutex_lock(&l->mu);
    int r = l->out_len > l->out_off;
    pthread_mutex_unlock(&l->mu);
    return r;
}

int dsnet_link_flush(dsnet_link *l) {
    pthread_mutex_lock(&l->mu);
    while (!l->dead && l->out_len > l->out_off) {
        long w = l->tr.write(l->tr.t, l->out + l->out_off, l->out_len - l->out_off);
        if (w < 0) l->dead = 1;
        else if (w == 0) break;
        else l->out_off += w;
    }
    int r = l->dead ? -1 : 0;
    pthread_mutex_unlock(&l->mu);
    return r;
}

int dsnet_link_fill(dsnet_link *l) {
    if (l->dead) return -1;
    for (;;) {
        if (reserve(&l->in, &l->in_off, &l->in_len, &l->in_cap, 65536)) return -1;
        long r = l->tr.read(l->tr.t, l->in + l->in_len, l->in_cap - l->in_len);
        if (r < 0) { l->dead = 1; return -1; }
        if (r == 0) return 0;
        l->in_len += r;
    }
}

int dsnet_link_next(dsnet_link *l, const uint8_t **body, uint32_t *len) {
    size_t avail = l->in_len - l->in_off;
    if (avail < 4) return 0;
    uint32_t n = get32(l->in + l->in_off);
    if (n == 0 || n > MAX_FRAME) { l->dead = 1; return 0; }
    if (avail < 4 + (size_t)n) return 0;
    *body = l->in + l->in_off + 4; *len = n;
    l->in_off += 4 + n;      /* body stays valid until the next fill */
    return 1;
}

int dsnet_blob_feed(dsnet_blob_rx *rx, const uint8_t *p, uint32_t n) {
    if (n < BLOB_HDR) return -1;
    uint32_t id = get32(p), total = get32(p + 5), off = get32(p + 9);
    uint32_t dlen = n - BLOB_HDR;
    if (total > DSNET_MAX_BLOB || dlen > DSNET_FRAG) return -1;
    if (off == 0) {
        free(rx->buf);
        memset(rx, 0, sizeof *rx);
        rx->id = id; rx->kind = p[4]; rx->total = total;
        rx->tick = get32(p + 13); rx->seq_after = get32(p + 17);
        rx->buf = malloc(total ? total : 1);
        if (!rx->buf) return -1;
    }
    if (!rx->buf || id != rx->id || off != rx->got || off + dlen > rx->total) return -1;
    memcpy(rx->buf + off, p + BLOB_HDR, dlen);
    rx->got += dlen;
    return rx->got == rx->total;
}
