// Control socket: one Unix socket per instance, one JSON object per line in, one per line out.
// A thread per connection reads lines and queues them; the game's main thread runs them in
// rig_drain() (MainFlow.Update hook) and hands the answer back. "ping" and "quit" are answered here.
#include "rig.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static RigReq *qhead, *qtail;

RigReq *rig_take_requests(void) {
    pthread_mutex_lock(&mu);
    RigReq *r = qhead; qhead = qtail = NULL;
    pthread_mutex_unlock(&mu);
    return r;
}
void rig_finish(RigReq *r, char *resp) {
    pthread_mutex_lock(&mu);
    r->resp = resp; r->done = 1;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
}

/* ---------------------------------------------------------------- JSON helpers */
static const char *jfind(const char *j, const char *key) {
    char k[80]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = strstr(j, k); if (!p) return NULL;
    p += strlen(k); while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return NULL;
    p++; while (*p == ' ' || *p == '\t') p++;
    return p;
}
int jstr(const char *j, const char *key, char *out, size_t n) {
    const char *p = jfind(j, key); if (!p || *p != '"') return 0;
    p++; size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        if (*p == '\\' && p[1]) { p++; out[i++] = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p; p++; continue; }
        out[i++] = *p++;
    }
    out[i] = 0; return 1;
}
int jnum(const char *j, const char *key, double *out) {
    const char *p = jfind(j, key); if (!p) return 0;
    char *e; double v = strtod(p, &e); if (e == p) return 0;
    *out = v; return 1;
}

void bput(Buf *b, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        size_t room = b->cap - b->n;
        va_start(ap, fmt); int k = vsnprintf(b->p ? b->p + b->n : NULL, b->p ? room : 0, fmt, ap); va_end(ap);
        if (k < 0) return;
        if (b->p && (size_t)k < room) { b->n += (size_t)k; return; }
        size_t nc = b->cap ? b->cap * 2 : 4096; while (nc < b->n + (size_t)k + 1) nc *= 2;
        b->p = realloc(b->p, nc); b->cap = nc;
    }
}
void bstr(Buf *b, const char *s) {
    bput(b, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') bput(b, "\\%c", c);
        else if (c < 0x20) bput(b, "\\u%04x", c);
        else bput(b, "%c", c);
    }
    bput(b, "\"");
}

/* ---------------------------------------------------------------- connections */
static void send_all(int fd, const char *s, size_t n) {
    while (n) { ssize_t k = write(fd, s, n); if (k <= 0) { if (errno == EINTR) continue; return; } s += k; n -= (size_t)k; }
}

static char *run_on_main(const char *line) {
    RigReq *r = calloc(1, sizeof *r); r->line = strdup(line);
    pthread_mutex_lock(&mu);
    if (qtail) qtail->next = r; else qhead = r;
    qtail = r;
    struct timespec until; clock_gettime(CLOCK_REALTIME, &until); until.tv_sec += 60;
    while (!r->done) if (pthread_cond_timedwait(&cv, &mu, &until) == ETIMEDOUT) break;
    int done = r->done;
    pthread_mutex_unlock(&mu);
    if (!done) return strdup("{\"ok\":false,\"error\":\"main thread did not answer in 60 s\"}");   /* r leaks on purpose: main may still use it */
    char *resp = r->resp; free(r->line); free(r);
    return resp ? resp : strdup("{\"ok\":false,\"error\":\"no response\"}");
}

static void *conn_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    char *buf = malloc(1 << 16); size_t n = 0;
    for (;;) {
        ssize_t k = read(fd, buf + n, (1 << 16) - 1 - n);
        if (k <= 0) break;
        n += (size_t)k; buf[n] = 0;
        char *nl;
        while ((nl = strchr(buf, '\n'))) {
            *nl = 0;
            char cmd[64] = "";
            jstr(buf, "cmd", cmd, sizeof cmd);
            char *resp;
            if (!strcmp(cmd, "ping")) {
                Buf b = {0}; bput(&b, "{\"ok\":true,\"pong\":true,\"pid\":%d,\"frames\":%lld}", getpid(), (long long)rig_frames); resp = b.p;
            } else if (!strcmp(cmd, "quit")) {
                const char *bye = "{\"ok\":true,\"quit\":true}\n";
                send_all(fd, bye, strlen(bye));
                rig_log("quit requested on the socket: exiting");
                _exit(0);
            } else resp = run_on_main(buf);
            send_all(fd, resp, strlen(resp)); send_all(fd, "\n", 1);
            free(resp);
            size_t used = (size_t)(nl - buf) + 1;
            memmove(buf, nl + 1, n - used + 1); n -= used;
        }
        if (n >= (1 << 16) - 1) n = 0;   /* line too long: drop it */
    }
    close(fd); free(buf);
    return NULL;
}

static char sock_path[1100];
static void *accept_thread(void *arg) {
    (void)arg;
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = {0}; a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", sock_path);
    unlink(sock_path);
    if (bind(s, (struct sockaddr*)&a, sizeof a) || listen(s, 8)) { rig_log("socket %s: bind/listen failed (%s)", sock_path, strerror(errno)); return NULL; }
    rig_log("control socket %s listening", sock_path);
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; rig_log("accept failed (%s)", strerror(errno)); sleep(1); continue; }
        int one = 1; setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);   /* client gone: EPIPE, not SIGPIPE (killed copies) */
        pthread_t t; pthread_create(&t, NULL, conn_thread, (void*)(intptr_t)c); pthread_detach(t);
    }
    return NULL;
}

void rig_sock_start(const char *path) {
    snprintf(sock_path, sizeof sock_path, "%s", path);
    pthread_t t; pthread_create(&t, NULL, accept_thread, NULL); pthread_detach(t);
}
