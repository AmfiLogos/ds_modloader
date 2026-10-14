/* Steam P2P transport + lobby for dsnet. See dsnet_steam.h. Flat API of Steamworks SDK 1.62
 * (Steamworks.NET 2025.162.1 in the game), resolved with dlsym from the game's libsteam_api. */
#include "dsnet_steam.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

/* ---- Steam constants and the few struct offsets we read (same for pack 4 and pack 8) ---- */
enum {
    k_EResultOK = 1, k_EResultLimitExceeded = 25,
    k_ELobbyTypeFriendsOnly = 1,
    k_EFriendFlagImmediate = 0x04,
    k_SendUnreliableNoNagle = 1, k_SendReliableNoNagle = 9,
    k_StateConnecting = 1, k_StateFindingRoute = 2, k_StateConnected = 3,
    k_StateClosedByPeer = 4, k_StateProblemDetectedLocally = 5,
    k_CfgSendBufferSize = 9, k_CfgCallbackConnectionStatusChanged = 201,
    k_CfgTypeInt32 = 1, k_CfgTypePtr = 5,
    k_iSteamAPICallCompleted = 703, k_iLobbyCreated = 513, k_iLobbyEnter = 504,
    k_iGameLobbyJoinRequested = 333, k_iGameRichPresenceJoinRequested = 337,
    k_EChatRoomEnterResponseSuccess = 1,
};
#define IDENT_SIZE   136               /* SteamNetworkingIdentity */
#define INFO_SIZE    696               /* SteamNetConnectionInfo_t */
#define INFO_LISTEN  144               /* m_hListenSocket */
#define INFO_STATE   176               /* m_eState */
#define INFO_DESC    312               /* m_szConnectionDescription */
#define MSG_MAX      65536             /* bytes per reliable message (SNS limit is 512 KB) */
#define BRIDGE_CAP   (1u << 20)        /* per-direction buffer before back-pressure */
#define MAX_BRIDGES  64
#define MAX_FRAME_B  (DSNET_MAX_PAYLOAD + 64)

typedef struct { int32_t user; int32_t id; uint8_t *param; int32_t cub; } callback_msg;   /* CallbackMsg_t */
typedef struct { int32_t value, type; union { int32_t i32; int64_t i64; void *ptr; } v; } cfg_value;
typedef struct { uint64_t game_id; uint32_t ip; uint16_t port, qport; uint64_t lobby; } __attribute__((packed, aligned(4))) friend_game_info;

typedef void *I;
static struct {
    void *lib;
    bool (*IsSteamRunning)(void);
    int  (*InitFlat)(char *err);
    void (*Shutdown)(void);
    void (*MD_Init)(void);
    void (*MD_RunFrame)(int32_t);
    bool (*MD_GetNextCallback)(int32_t, callback_msg *);
    void (*MD_FreeLastCallback)(int32_t);
    bool (*MD_GetAPICallResult)(int32_t, uint64_t, void *, int, int, bool *);
    void *(*CreateInterface)(const char *);
    int32_t (*CreateSteamPipe)(I);
    int32_t (*ConnectToGlobalUser)(I, int32_t);
    I (*GetISteamMatchmaking)(I, int32_t, int32_t, const char *);
    I (*GetISteamFriends)(I, int32_t, int32_t, const char *);
    void (*ReleaseUser)(I, int32_t, int32_t);
    bool (*BReleaseSteamPipe)(I, int32_t);
    I (*SteamUser)(void);
    I (*SteamFriends)(void);
    I (*SteamMatchmaking)(void);
    I (*SteamApps)(void);
    I (*SteamUtils)(void);
    I (*SNS)(void);
    I (*SNU)(void);
    bool (*BLoggedOn)(I);
    uint64_t (*GetSteamID)(I);
    uint32_t (*GetAppID)(I);
    uint64_t (*CreateLobby)(I, int, int);
    uint64_t (*JoinLobby)(I, uint64_t);
    void (*LeaveLobby)(I, uint64_t);
    bool (*SetLobbyData)(I, uint64_t, const char *, const char *);
    const char *(*GetLobbyData)(I, uint64_t, const char *);
    uint64_t (*GetLobbyOwner)(I, uint64_t);
    void (*ActivateGameOverlayInviteDialog)(I, uint64_t);
    int  (*GetFriendCount)(I, int);
    uint64_t (*GetFriendByIndex)(I, int, int);
    bool (*GetFriendGamePlayed)(I, uint64_t, friend_game_info *);
    const char *(*GetFriendPersonaName)(I, uint64_t);
    int  (*GetLaunchCommandLine)(I, char *, int);
    uint32_t (*CreateListenSocketP2P)(I, int, int, const cfg_value *);
    uint32_t (*ConnectP2P)(I, const void *, int, int, const cfg_value *);
    int  (*AcceptConnection)(I, uint32_t);
    bool (*CloseConnection)(I, uint32_t, int, const char *, bool);
    bool (*CloseListenSocket)(I, uint32_t);
    int  (*SendMessageToConnection)(I, uint32_t, const void *, uint32_t, int, int64_t *);
    int  (*ReceiveMessagesOnConnection)(I, uint32_t, void **, int);
    bool (*GetConnectionInfo)(I, uint32_t, void *);
    void (*RunCallbacks)(I);
    bool (*CreateSocketPair)(I, uint32_t *, uint32_t *, bool, const void *, const void *);
    void (*InitRelayNetworkAccess)(I);
    int  (*GetRelayNetworkStatus)(I, void *);
    void (*SetSteamID64)(void *, uint64_t);
    bool (*InviteUserToLobby)(I, uint64_t, uint64_t);
    const char *(*GetPersonaName)(I);
    int32_t (*GetHSteamPipe)(void);
    void (*MsgRelease)(void *);
} S;

static const struct { const char *name; size_t off; } SYMS[] = {
#define SYM(field, name) { name, offsetof(__typeof__(S), field) }
    SYM(IsSteamRunning, "SteamAPI_IsSteamRunning"), SYM(InitFlat, "SteamAPI_InitFlat"),
    SYM(Shutdown, "SteamAPI_Shutdown"), SYM(MD_Init, "SteamAPI_ManualDispatch_Init"),
    SYM(MD_RunFrame, "SteamAPI_ManualDispatch_RunFrame"),
    SYM(MD_GetNextCallback, "SteamAPI_ManualDispatch_GetNextCallback"),
    SYM(MD_FreeLastCallback, "SteamAPI_ManualDispatch_FreeLastCallback"),
    SYM(MD_GetAPICallResult, "SteamAPI_ManualDispatch_GetAPICallResult"),
    SYM(CreateInterface, "SteamInternal_CreateInterface"),
    SYM(CreateSteamPipe, "SteamAPI_ISteamClient_CreateSteamPipe"),
    SYM(ConnectToGlobalUser, "SteamAPI_ISteamClient_ConnectToGlobalUser"),
    SYM(GetISteamMatchmaking, "SteamAPI_ISteamClient_GetISteamMatchmaking"),
    SYM(GetISteamFriends, "SteamAPI_ISteamClient_GetISteamFriends"),
    SYM(ReleaseUser, "SteamAPI_ISteamClient_ReleaseUser"),
    SYM(BReleaseSteamPipe, "SteamAPI_ISteamClient_BReleaseSteamPipe"),
    SYM(SteamUser, "SteamAPI_SteamUser_v023"), SYM(SteamFriends, "SteamAPI_SteamFriends_v018"),
    SYM(SteamMatchmaking, "SteamAPI_SteamMatchmaking_v009"), SYM(SteamApps, "SteamAPI_SteamApps_v008"),
    SYM(SteamUtils, "SteamAPI_SteamUtils_v010"),
    SYM(SNS, "SteamAPI_SteamNetworkingSockets_SteamAPI_v012"),
    SYM(SNU, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004"),
    SYM(BLoggedOn, "SteamAPI_ISteamUser_BLoggedOn"), SYM(GetSteamID, "SteamAPI_ISteamUser_GetSteamID"),
    SYM(GetAppID, "SteamAPI_ISteamUtils_GetAppID"),
    SYM(CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby"),
    SYM(JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby"),
    SYM(LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby"),
    SYM(SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData"),
    SYM(GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData"),
    SYM(GetLobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner"),
    SYM(ActivateGameOverlayInviteDialog, "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog"),
    SYM(GetFriendCount, "SteamAPI_ISteamFriends_GetFriendCount"),
    SYM(GetFriendByIndex, "SteamAPI_ISteamFriends_GetFriendByIndex"),
    SYM(GetFriendGamePlayed, "SteamAPI_ISteamFriends_GetFriendGamePlayed"),
    SYM(GetFriendPersonaName, "SteamAPI_ISteamFriends_GetFriendPersonaName"),
    SYM(GetLaunchCommandLine, "SteamAPI_ISteamApps_GetLaunchCommandLine"),
    SYM(CreateListenSocketP2P, "SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P"),
    SYM(ConnectP2P, "SteamAPI_ISteamNetworkingSockets_ConnectP2P"),
    SYM(AcceptConnection, "SteamAPI_ISteamNetworkingSockets_AcceptConnection"),
    SYM(CloseConnection, "SteamAPI_ISteamNetworkingSockets_CloseConnection"),
    SYM(CloseListenSocket, "SteamAPI_ISteamNetworkingSockets_CloseListenSocket"),
    SYM(SendMessageToConnection, "SteamAPI_ISteamNetworkingSockets_SendMessageToConnection"),
    SYM(ReceiveMessagesOnConnection, "SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection"),
    SYM(GetConnectionInfo, "SteamAPI_ISteamNetworkingSockets_GetConnectionInfo"),
    SYM(RunCallbacks, "SteamAPI_ISteamNetworkingSockets_RunCallbacks"),
    SYM(CreateSocketPair, "SteamAPI_ISteamNetworkingSockets_CreateSocketPair"),
    SYM(InitRelayNetworkAccess, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess"),
    SYM(GetRelayNetworkStatus, "SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus"),
    SYM(SetSteamID64, "SteamAPI_SteamNetworkingIdentity_SetSteamID64"),
    SYM(MsgRelease, "SteamAPI_SteamNetworkingMessage_t_Release"),
    SYM(InviteUserToLobby, "SteamAPI_ISteamMatchmaking_InviteUserToLobby"),
    SYM(GetPersonaName, "SteamAPI_ISteamFriends_GetPersonaName"),
    SYM(GetHSteamPipe, "SteamAPI_GetHSteamPipe"),
#undef SYM
};

static char g_err[256];
const char *dsnet_steam_error(void) { return g_err; }

int dsnet_steam_load(const char *path) {
    if (S.lib) return 0;
    void *lib = NULL;
    if (path) lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    else {
        for (uint32_t i = 0, n = _dyld_image_count(); i < n && !lib; i++) {
            const char *img = _dyld_get_image_name(i);
            if (img && strstr(img, "libsteam_api.dylib")) lib = dlopen(img, RTLD_NOLOAD | RTLD_NOW);
        }
    }
    if (!lib) { snprintf(g_err, sizeof g_err, "libsteam_api not %s: %s", path ? "loadable" : "loaded in this process", path ? dlerror() : ""); return -1; }
    for (size_t i = 0; i < sizeof SYMS / sizeof *SYMS; i++) {
        void *f = dlsym(lib, SYMS[i].name);
        if (!f) { snprintf(g_err, sizeof g_err, "missing symbol %s", SYMS[i].name); dlclose(lib); memset(&S, 0, sizeof S); return -1; }
        memcpy((char *)&S + SYMS[i].off, &f, sizeof f);
    }
    S.lib = lib;
    return 0;
}

int dsnet_steam_is_running(void) { return S.lib && S.IsSteamRunning(); }

int dsnet_steam_init_standalone(uint32_t appid) {
    if (!S.lib) { snprintf(g_err, sizeof g_err, "not loaded"); return -1; }
    char id[16]; snprintf(id, sizeof id, "%u", appid);
    setenv("SteamAppId", id, 1); setenv("SteamGameId", id, 1);
    char err[1024] = "";
    int r = S.InitFlat(err);
    if (r != 0) { snprintf(g_err, sizeof g_err, "SteamAPI_InitFlat = %d: %s", r, err); return -1; }
    S.MD_Init();
    return 0;
}
void dsnet_steam_shutdown_standalone(void) { if (S.lib) S.Shutdown(); }

int      dsnet_steam_logged_on(void) { return S.lib && S.SteamUser() && S.BLoggedOn(S.SteamUser()); }
uint64_t dsnet_steam_my_id(void) { return S.lib && S.SteamUser() ? S.GetSteamID(S.SteamUser()) : 0; }
const char *dsnet_steam_my_name(void) {
    const char *n = S.lib && S.SteamFriends() ? S.GetPersonaName(S.SteamFriends()) : NULL;
    return n ? n : "";
}
int dsnet_steam_initialized(void) { return S.lib && S.GetHSteamPipe() != 0; }
int dsnet_steam_relay_status(void) {
    if (!S.lib) return 0;
    uint8_t details[1024];
    S.InitRelayNetworkAccess(S.SNU());
    return S.GetRelayNetworkStatus(S.SNU(), details);
}

/* ---- transport ---- */
typedef struct steam_tr {
    uint32_t conn;
    int dead, all_reliable;
    void *q[16]; int qi, qn; uint32_t qoff;    /* received messages not yet read */
    double last_check;
} steam_tr;

static int conn_state(uint32_t conn, uint32_t *listen, char *desc, size_t dn) {
    uint8_t info[INFO_SIZE + 64];
    if (!S.GetConnectionInfo(S.SNS(), conn, info)) return -1;
    if (listen) memcpy(listen, info + INFO_LISTEN, 4);
    if (desc) snprintf(desc, dn, "%.127s", (const char *)info + INFO_DESC);
    int32_t st; memcpy(&st, info + INFO_STATE, 4);
    return st;
}
static int state_dead(int st) { return st < k_StateConnecting || st > k_StateConnected; }

static int unreliable_type(uint8_t t) { return t == P_TIMECTL || t == P_KEEPALIVE || t == P_FRAMETIME; }

/* buf holds whole link frames (u32 LE length + body). Sends a prefix of whole frames. */
static long st_write(void *t, const void *buf, size_t n) {
    steam_tr *s = t;
    if (s->dead) return -1;
    const uint8_t *p = buf;
    size_t off = 0;
    while (n - off >= 5) {
        uint32_t fl = get32(p + off);
        if (fl == 0 || fl > MAX_FRAME_B) { s->dead = 1; return -1; }
        if (n - off < 4 + (size_t)fl) break;
        int unrel = !s->all_reliable && unreliable_type(p[off + 4]);
        size_t end = off + 4 + fl;
        while (!unrel && n - end >= 5) {           /* batch following reliable frames */
            uint32_t l2 = get32(p + end);
            if (l2 == 0 || l2 > MAX_FRAME_B || n - end < 4 + (size_t)l2) break;
            if ((!s->all_reliable && unreliable_type(p[end + 4])) || end + 4 + l2 - off > MSG_MAX) break;
            end += 4 + l2;
        }
        int r = S.SendMessageToConnection(S.SNS(), s->conn, p + off, (uint32_t)(end - off),
                                          unrel ? k_SendUnreliableNoNagle : k_SendReliableNoNagle, NULL);
        if (r == k_EResultOK || (unrel && r == k_EResultLimitExceeded)) off = end;   /* unreliable may drop */
        else if (r == k_EResultLimitExceeded) break;                                    /* send buffer full */
        else { dsnet_logf("steam: send on %u failed, EResult %d", s->conn, r); s->dead = 1; return off ? (long)off : -1; }
    }
    return (long)off;
}

static long st_read(void *t, void *buf, size_t n) {
    steam_tr *s = t;
    size_t got = 0;
    while (got < n) {
        if (s->qi == s->qn) {
            s->qi = s->qn = 0; s->qoff = 0;
            int r = S.ReceiveMessagesOnConnection(S.SNS(), s->conn, s->q, 16);
            if (r < 0) { s->dead = 1; break; }
            if (r == 0) break;
            s->qn = r;
        }
        void *m = s->q[s->qi];
        const uint8_t *data; int32_t size;
        memcpy(&data, m, sizeof data); memcpy(&size, (uint8_t *)m + 8, 4);   /* m_pData, m_cbSize */
        size_t k = (size_t)size - s->qoff; if (k > n - got) k = n - got;
        memcpy((uint8_t *)buf + got, data + s->qoff, k);
        got += k; s->qoff += k;
        if (s->qoff >= (uint32_t)size) { S.MsgRelease(m); s->qi++; s->qoff = 0; }
    }
    if (got) return (long)got;
    double now = dsnet_now_ms();
    if (!s->dead && now - s->last_check >= 20) {
        s->last_check = now;
        int st = conn_state(s->conn, NULL, NULL, 0);
        if (state_dead(st)) { dsnet_logf("steam: connection %u closed (state %d)", s->conn, st); s->dead = 1; }
    }
    return s->dead ? -1 : 0;
}

static int  st_poll(void *t) { return -1; }   /* SNS has no fd: dsnet loops time out (4-10 ms) */
static void st_close(void *t) {
    steam_tr *s = t;
    for (; s->qi < s->qn; s->qi++) S.MsgRelease(s->q[s->qi]);
    S.CloseConnection(S.SNS(), s->conn, 0, "dsnet close", true);
    free(s);
}

dsnet_transport dsnet_steam_transport(uint32_t hconn) {
    steam_tr *s = calloc(1, sizeof *s);
    s->conn = hconn;
    const char *e = getenv("DSNET_STEAM_ALL_RELIABLE");
    s->all_reliable = e && *e == '1';
    return (dsnet_transport){ st_write, st_read, st_poll, st_close, s };
}

/* ---- module state: private pipe, pump thread, bridges ---- */
typedef struct { uint8_t *b; size_t off, len, cap; } bq;
static size_t bq_used(bq *q) { return q->len - q->off; }
static int bq_reserve(bq *q, size_t need) {
    if (q->off && q->off == q->len) q->off = q->len = 0;
    if (q->len + need <= q->cap) return 0;
    if (q->off) { memmove(q->b, q->b + q->off, q->len - q->off); q->len -= q->off; q->off = 0; }
    if (q->len + need <= q->cap) return 0;
    size_t nc = q->cap ? q->cap : 65536;
    while (nc < q->len + need) nc *= 2;
    uint8_t *nb = realloc(q->b, nc);
    if (!nb) return -1;
    q->b = nb; q->cap = nc;
    return 0;
}

struct dsnet_steam_host;
struct dsnet_steam_join;
typedef struct bridge {
    int used;
    uint32_t conn;
    dsnet_transport tr;
    int fd, listen_fd;
    bq to_steam, to_fd;
    struct dsnet_steam_host *host;
    struct dsnet_steam_join *join;
} bridge;

struct dsnet_steam_host {
    uint16_t port; int max_members;
    char build[32]; uint32_t mod;
    uint32_t listen;
    uint64_t create_call, lobby;
};
enum { J_LOBBY, J_CONNECTING, J_READY, J_FAILED };
struct dsnet_steam_join {
    int state; char err[128];
    uint64_t call, lobby, host_id;
    char build[32]; uint32_t mod;
    uint16_t port;
    bridge *br;
    double t_connect;
};

static pthread_mutex_t G_mu;
static pthread_once_t  G_once = PTHREAD_ONCE_INIT;
static pthread_t G_th;
static int G_users, G_running;
static I   G_client, G_mm, G_friends;
static int32_t G_pipe, G_user;
static bridge G_br[MAX_BRIDGES];
static dsnet_steam_host *G_hosts[8];
static dsnet_steam_join *G_joins[16];
static uint64_t G_join_req;
static int G_args_checked;

static void g_init(void) {
    pthread_mutexattr_t a; pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);   /* SNS callback may run inside our calls */
    pthread_mutex_init(&G_mu, &a);
}
#define LOCK()   (pthread_once(&G_once, g_init), pthread_mutex_lock(&G_mu))
#define UNLOCK() pthread_mutex_unlock(&G_mu)

static void bridge_close(bridge *b) {
    if (!b->used) return;
    if (b->fd >= 0) close(b->fd);
    if (b->listen_fd >= 0) close(b->listen_fd);
    b->tr.close(b->tr.t);
    free(b->to_steam.b); free(b->to_fd.b);
    if (b->join) {
        b->join->br = NULL;
        if (b->join->state != J_FAILED) { b->join->state = J_FAILED; snprintf(b->join->err, sizeof b->join->err, "Steam connection closed"); }
    }
    memset(b, 0, sizeof *b);
}

static bridge *bridge_new(uint32_t conn, int fd, int listen_fd) {
    for (int i = 0; i < MAX_BRIDGES; i++) if (!G_br[i].used) {
        bridge *b = &G_br[i];
        memset(b, 0, sizeof *b);
        b->used = 1; b->conn = conn; b->tr = dsnet_steam_transport(conn);
        b->fd = fd; b->listen_fd = listen_fd;
        return b;
    }
    return NULL;
}

static void sock_opts(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

static int loopback_connect(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a)) { if (fd >= 0) close(fd); return -1; }
    sock_opts(fd);
    return fd;
}

static int loopback_listen(uint16_t *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t al = sizeof a;
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) || listen(fd, 1) ||
        getsockname(fd, (struct sockaddr *)&a, &al)) { if (fd >= 0) close(fd); return -1; }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    *port = ntohs(a.sin_port);
    return fd;
}

/* 1 = did something */
static int bridge_pump(bridge *b) {
    int busy = 0;
    if (b->listen_fd >= 0) {
        int fd = accept(b->listen_fd, NULL, NULL);
        if (fd >= 0) { sock_opts(fd); b->fd = fd; close(b->listen_fd); b->listen_fd = -1; busy = 1; }
    }
    int dead = 0;
    if (b->fd >= 0) {                                   /* dsnet -> Steam */
        while (bq_used(&b->to_steam) < BRIDGE_CAP) {
            if (bq_reserve(&b->to_steam, 65536)) { dead = 1; break; }
            ssize_t r = recv(b->fd, b->to_steam.b + b->to_steam.len, b->to_steam.cap - b->to_steam.len, 0);
            if (r > 0) { b->to_steam.len += r; busy = 1; continue; }
            if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) dead = 1;
            break;
        }
    }
    if (bq_used(&b->to_steam)) {
        long w = b->tr.write(b->tr.t, b->to_steam.b + b->to_steam.off, bq_used(&b->to_steam));
        if (w < 0) dead = 1; else if (w) { b->to_steam.off += w; busy = 1; }
    }
    while (!dead && bq_used(&b->to_fd) < BRIDGE_CAP) {  /* Steam -> dsnet */
        if (bq_reserve(&b->to_fd, 65536)) { dead = 1; break; }
        long r = b->tr.read(b->tr.t, b->to_fd.b + b->to_fd.len, b->to_fd.cap - b->to_fd.len);
        if (r < 0) dead = 1;
        if (r <= 0) break;
        b->to_fd.len += r; busy = 1;
    }
    if (b->fd >= 0 && bq_used(&b->to_fd)) {
        ssize_t w = send(b->fd, b->to_fd.b + b->to_fd.off, bq_used(&b->to_fd), 0);
        if (w > 0) { b->to_fd.off += w; busy = 1; }
        else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) dead = 1;
    }
    if (dead) bridge_close(b);
    return busy;
}

static dsnet_steam_host *host_of_listen(uint32_t listen) {
    for (int i = 0; i < 8; i++) if (G_hosts[i] && G_hosts[i]->listen == listen) return G_hosts[i];
    return NULL;
}
static int host_peer_count(dsnet_steam_host *h) {
    int n = 0;
    for (int i = 0; i < MAX_BRIDGES; i++) if (G_br[i].used && G_br[i].host == h) n++;
    return n;
}
static int host_bridge(dsnet_steam_host *h, uint32_t conn) {
    int fd = loopback_connect(h->port);
    bridge *b = fd >= 0 ? bridge_new(conn, fd, -1) : NULL;
    if (!b) { if (fd >= 0) close(fd); return -1; }
    b->host = h;
    return 0;
}

/* SteamNetConnectionStatusChangedCallback_t: only m_hConn (offset 0) is read; the rest is
 * queried with GetConnectionInfo so the callback struct's packing does not matter. */
static void on_conn_status(void *cb) {
    uint32_t conn; memcpy(&conn, cb, 4);
    LOCK();
    uint32_t listen = 0; char desc[128];
    int st = conn_state(conn, &listen, desc, sizeof desc);
    dsnet_steam_host *h = listen ? host_of_listen(listen) : NULL;
    if (h && st == k_StateConnecting) {
        int ok = host_peer_count(h) < h->max_members && S.AcceptConnection(S.SNS(), conn) == k_EResultOK
                 && host_bridge(h, conn) == 0;
        dsnet_logf("steam: incoming %s %s", desc, ok ? "accepted" : "refused");
        if (!ok) S.CloseConnection(S.SNS(), conn, 0, "dsnet host full or server down", false);
    } else if (state_dead(st)) {
        int ours = 0;
        for (int i = 0; i < MAX_BRIDGES; i++) if (G_br[i].used && G_br[i].conn == conn) ours = 1;
        if (!ours && h) S.CloseConnection(S.SNS(), conn, 0, NULL, false);   /* never accepted */
    }
    UNLOCK();
}

static void parse_connect(const char *s) {
    const char *p = s ? strstr(s, "+connect_lobby") : NULL;
    if (!p) return;
    uint64_t id = strtoull(p + 14, NULL, 10);
    if (id) G_join_req = id;
}

void dsnet_steam_feed_callback(int id, const void *param, int cub) {
    const uint8_t *p = param;
    if (!p || (id != k_iGameLobbyJoinRequested && id != k_iGameRichPresenceJoinRequested)) return;
    LOCK();
    if (id == k_iGameLobbyJoinRequested && cub >= 8) {
        uint64_t lobby; memcpy(&lobby, p, 8);                         /* m_steamIDLobby */
        if (lobby) G_join_req = lobby;
        dsnet_logf("steam: invite accepted, lobby %llu", (unsigned long long)lobby);
    } else if (id == k_iGameRichPresenceJoinRequested && cub >= 8) {
        char s[257]; size_t n = cub - 8 < 256 ? (size_t)cub - 8 : 256;   /* friend id, then rgchConnect */
        memcpy(s, p + 8, n); s[n] = 0; parse_connect(s);
    }
    UNLOCK();
}

static void join_on_enter(dsnet_steam_join *j, const uint8_t *r, int cub, bool failed) {
    uint32_t resp = 0;
    if (!failed && cub >= 20) memcpy(&resp, r + 16, 4);         /* LobbyEnter_t.m_EChatRoomEnterResponse */
    if (resp != k_EChatRoomEnterResponseSuccess) {
        j->state = J_FAILED; snprintf(j->err, sizeof j->err, "could not enter lobby (response %u)", resp); return;
    }
    I mm = S.SteamMatchmaking();
    const char *b = S.GetLobbyData(mm, j->lobby, "dsmp_build"), *m = S.GetLobbyData(mm, j->lobby, "dsmp_mod");
    if (j->build[0] && b && *b && strcmp(b, j->build)) {
        j->state = J_FAILED; snprintf(j->err, sizeof j->err, "host runs game build %s, you run %s", b, j->build); return;
    }
    if (j->mod && m && *m && strtoul(m, NULL, 10) != j->mod) {
        j->state = J_FAILED; snprintf(j->err, sizeof j->err, "host runs mod version %s, you run %u", m, j->mod); return;
    }
    j->host_id = S.GetLobbyOwner(mm, j->lobby);
    if (!j->host_id || j->host_id == dsnet_steam_my_id()) {
        j->state = J_FAILED; snprintf(j->err, sizeof j->err, "lobby has no other owner"); return;
    }
}

static void pump_pipe(void) {
    if (!G_pipe) return;
    S.MD_RunFrame(G_pipe);
    callback_msg m;
    while (S.MD_GetNextCallback(G_pipe, &m)) {
        if (m.id == k_iSteamAPICallCompleted && m.cub >= 16) {
            uint64_t call; int32_t id; uint32_t cub;
            memcpy(&call, m.param, 8); memcpy(&id, m.param + 8, 4); memcpy(&cub, m.param + 12, 4);
            uint8_t *r = calloc(1, cub + 8); bool failed = true;
            if (r && S.MD_GetAPICallResult(G_pipe, call, r, (int)cub, id, &failed)) {
                for (int i = 0; i < 8; i++) {
                    dsnet_steam_host *h = G_hosts[i];
                    if (!h || h->create_call != call) continue;
                    int32_t res; uint64_t lobby = 0;
                    memcpy(&res, r, 4);                                   /* LobbyCreated_t */
                    memcpy(&lobby, r + (cub >= 16 ? 8 : 4), 8);           /* pack 4 on macOS: offset 4 */
                    h->create_call = 0;
                    if (failed || res != k_EResultOK || !lobby) { h->lobby = ~0ull; dsnet_logf("steam: CreateLobby failed (EResult %d)", res); continue; }
                    h->lobby = lobby;
                    char v[16]; I mm = S.SteamMatchmaking();
                    snprintf(v, sizeof v, "%d", DSNET_PROTO); S.SetLobbyData(mm, lobby, "dsmp_proto", v);
                    S.SetLobbyData(mm, lobby, "dsmp_build", h->build);
                    snprintf(v, sizeof v, "%u", h->mod); S.SetLobbyData(mm, lobby, "dsmp_mod", v);
                    char sid[24]; snprintf(sid, sizeof sid, "%llu", (unsigned long long)dsnet_steam_my_id());
                    S.SetLobbyData(mm, lobby, "dsmp_host", sid);
                    dsnet_logf("steam: lobby %llu created", (unsigned long long)lobby);
                }
                for (int i = 0; i < 16; i++) {
                    dsnet_steam_join *j = G_joins[i];
                    if (j && j->call == call) { j->call = 0; join_on_enter(j, r, (int)cub, failed); }
                }
            }
            free(r);
        } else dsnet_steam_feed_callback(m.id, m.param, m.cub);
        S.MD_FreeLastCallback(G_pipe);
    }
}

#define CONNECT_TIMEOUT_MS 30000
static void join_step(dsnet_steam_join *j) {
    if (j->state == J_CONNECTING) {          /* ready = SNS connected (route found), so dsnet's HELLO wait is short */
        int st = j->br ? conn_state(j->br->conn, NULL, NULL, 0) : -1;
        if (st == k_StateConnected) { j->state = J_READY; dsnet_logf("steam: connected to host"); }
        else if (state_dead(st) || dsnet_now_ms() - j->t_connect > CONNECT_TIMEOUT_MS) {
            if (j->br) bridge_close(j->br);
            j->state = J_FAILED; snprintf(j->err, sizeof j->err, "could not reach the host over Steam (state %d)", st);
        }
        return;
    }
    if (j->state != J_LOBBY || j->call || !j->host_id) return;
    uint8_t ident[IDENT_SIZE] __attribute__((aligned(8))) = {0};
    S.SetSteamID64(ident, j->host_id);
    cfg_value opt[2] = { { k_CfgCallbackConnectionStatusChanged, k_CfgTypePtr, { .ptr = NULL } },
                         { k_CfgSendBufferSize, k_CfgTypeInt32, { .i32 = 4 << 20 } } };
    void (*fn)(void *) = on_conn_status; memcpy(&opt[0].v.ptr, &fn, sizeof fn);
    uint32_t conn = S.ConnectP2P(S.SNS(), ident, DSNET_STEAM_VPORT, 2, opt);
    uint16_t port = 0;
    int lfd = conn ? loopback_listen(&port) : -1;
    bridge *b = lfd >= 0 ? bridge_new(conn, -1, lfd) : NULL;
    if (!b) {
        if (lfd >= 0) close(lfd);
        if (conn) S.CloseConnection(S.SNS(), conn, 0, NULL, false);
        j->state = J_FAILED; snprintf(j->err, sizeof j->err, "ConnectP2P failed"); return;
    }
    b->join = j; j->br = b; j->port = port; j->state = J_CONNECTING; j->t_connect = dsnet_now_ms();
    dsnet_logf("steam: connecting to %llu, dsnet on 127.0.0.1:%u", (unsigned long long)j->host_id, port);
}

static void *pump_main(void *arg) {
    while (1) {
        LOCK();
        int run = G_running;
        UNLOCK();
        if (!run) break;
        S.RunCallbacks(S.SNS());          /* may call on_conn_status (takes the lock itself) */
        LOCK();
        pump_pipe();
        for (int i = 0; i < 16; i++) if (G_joins[i]) join_step(G_joins[i]);
        int busy = 0;
        for (int i = 0; i < MAX_BRIDGES; i++) if (G_br[i].used) busy |= bridge_pump(&G_br[i]);
        UNLOCK();
        if (!busy) usleep(1000);
    }
    return NULL;
}

static int users_add(void) {
    LOCK();
    int ok = 0;
    if (!S.lib) snprintf(g_err, sizeof g_err, "dsnet_steam_load not called");
    else if (!G_users++) {
        G_client = S.CreateInterface("SteamClient021");
        G_pipe = G_client ? S.CreateSteamPipe(G_client) : 0;
        G_user = G_pipe ? S.ConnectToGlobalUser(G_client, G_pipe) : 0;
        G_mm = G_user ? S.GetISteamMatchmaking(G_client, G_user, G_pipe, "SteamMatchMaking009") : NULL;
        G_friends = G_user ? S.GetISteamFriends(G_client, G_user, G_pipe, "SteamFriends018") : NULL;
        if (!G_mm) {
            snprintf(g_err, sizeof g_err, "no private Steam pipe (client %p pipe %d user %d)", G_client, G_pipe, G_user);
            if (G_user) S.ReleaseUser(G_client, G_pipe, G_user);
            if (G_pipe) S.BReleaseSteamPipe(G_client, G_pipe);
            G_pipe = G_user = 0; G_users = 0;
        } else {
            S.InitRelayNetworkAccess(S.SNU());
            G_running = 1;
            pthread_create(&G_th, NULL, pump_main, NULL);
            ok = 1;
        }
    } else ok = 1;
    UNLOCK();
    return ok ? 0 : -1;
}

static void users_release(void) {
    LOCK();
    int stop = --G_users == 0;
    if (stop) G_running = 0;
    UNLOCK();
    if (!stop) return;
    pthread_join(G_th, NULL);
    LOCK();
    S.ReleaseUser(G_client, G_pipe, G_user);
    S.BReleaseSteamPipe(G_client, G_pipe);
    G_pipe = G_user = 0; G_mm = G_friends = NULL;
    UNLOCK();
}

/* ---- host ---- */
dsnet_steam_host *dsnet_steam_host_start(uint16_t server_port, const char *build_id,
                                         uint32_t mod_version, int max_members) {
    if (users_add()) return NULL;
    dsnet_steam_host *h = calloc(1, sizeof *h);
    h->port = server_port; h->max_members = max_members > 0 ? max_members : 8;
    snprintf(h->build, sizeof h->build, "%s", build_id ? build_id : dsnet_build_id);
    h->mod = mod_version ? mod_version : dsnet_mod_version;
    LOCK();
    int slot = -1;
    for (int i = 0; i < 8; i++) if (!G_hosts[i]) { slot = i; break; }
    cfg_value opt[2] = { { k_CfgCallbackConnectionStatusChanged, k_CfgTypePtr, { .ptr = NULL } },
                         { k_CfgSendBufferSize, k_CfgTypeInt32, { .i32 = 4 << 20 } } };
    void (*fn)(void *) = on_conn_status; memcpy(&opt[0].v.ptr, &fn, sizeof fn);
    if (slot >= 0) h->listen = S.CreateListenSocketP2P(S.SNS(), DSNET_STEAM_VPORT, 2, opt);
    if (slot < 0 || !h->listen) {
        UNLOCK();
        snprintf(g_err, sizeof g_err, "CreateListenSocketP2P failed");
        free(h); users_release(); return NULL;
    }
    G_hosts[slot] = h;
    h->create_call = S.CreateLobby(G_mm, k_ELobbyTypeFriendsOnly, h->max_members + 1);
    if (!h->create_call) h->lobby = ~0ull;
    UNLOCK();
    return h;
}

uint64_t dsnet_steam_host_lobby(dsnet_steam_host *h) { LOCK(); uint64_t l = h->lobby; UNLOCK(); return l; }
uint32_t dsnet_steam_host_listen_socket(dsnet_steam_host *h) { return h->listen; }
int dsnet_steam_host_peers(dsnet_steam_host *h) { LOCK(); int n = host_peer_count(h); UNLOCK(); return n; }
int dsnet_steam_host_invite(dsnet_steam_host *h) {
    LOCK(); uint64_t l = h->lobby; UNLOCK();
    if (!l || l == ~0ull) return -1;
    S.ActivateGameOverlayInviteDialog(S.SteamFriends(), l);
    return 0;
}
int dsnet_steam_host_invite_friend(dsnet_steam_host *h, uint64_t friend_id) {
    LOCK(); uint64_t l = h->lobby; UNLOCK();
    if (!l || l == ~0ull) return -1;
    return S.InviteUserToLobby(S.SteamMatchmaking(), l, friend_id) ? 0 : -1;
}
int dsnet_steam_friends(dsnet_steam_friend *out, int max) {
    if (!S.lib || !S.SteamFriends()) return 0;
    I f = S.SteamFriends();
    int n = 0, cnt = S.GetFriendCount(f, k_EFriendFlagImmediate);
    for (int i = 0; i < cnt && n < max; i++) {
        uint64_t id = S.GetFriendByIndex(f, i, k_EFriendFlagImmediate);
        friend_game_info gi; memset(&gi, 0, sizeof gi);
        out[n].steamid = id;
        out[n].lobby = S.GetFriendGamePlayed(f, id, &gi) ? gi.lobby : 0;
        snprintf(out[n].name, sizeof out[n].name, "%s", S.GetFriendPersonaName(f, id));
        n++;
    }
    return n;
}
void dsnet_steam_host_close_peers(dsnet_steam_host *h) {
    LOCK();
    for (int i = 0; i < MAX_BRIDGES; i++) if (G_br[i].used && G_br[i].host == h) bridge_close(&G_br[i]);
    UNLOCK();
}
int dsnet_steam_host_adopt(dsnet_steam_host *h, uint32_t hconn) {
    LOCK(); int r = host_bridge(h, hconn); UNLOCK(); return r;
}

void dsnet_steam_host_stop(dsnet_steam_host *h) {
    if (!h) return;
    LOCK();
    for (int i = 0; i < MAX_BRIDGES; i++) if (G_br[i].used && G_br[i].host == h) bridge_close(&G_br[i]);
    for (int i = 0; i < 8; i++) if (G_hosts[i] == h) G_hosts[i] = NULL;
    S.CloseListenSocket(S.SNS(), h->listen);
    if (h->lobby && h->lobby != ~0ull) S.LeaveLobby(G_mm, h->lobby);
    UNLOCK();
    free(h);
    users_release();
}

/* ---- joiner ---- */
static dsnet_steam_join *join_new(void) {
    if (users_add()) return NULL;
    dsnet_steam_join *j = calloc(1, sizeof *j);
    LOCK();
    int slot = -1;
    for (int i = 0; i < 16; i++) if (!G_joins[i]) { slot = i; break; }
    if (slot < 0) { UNLOCK(); free(j); users_release(); snprintf(g_err, sizeof g_err, "too many joins"); return NULL; }
    G_joins[slot] = j;
    UNLOCK();
    return j;
}

dsnet_steam_join *dsnet_steam_join_lobby(uint64_t lobby, const char *build_id, uint32_t mod_version) {
    dsnet_steam_join *j = join_new();
    if (!j) return NULL;
    LOCK();
    j->lobby = lobby;
    snprintf(j->build, sizeof j->build, "%s", build_id ? build_id : dsnet_build_id);
    j->mod = mod_version ? mod_version : dsnet_mod_version;
    j->call = S.JoinLobby(G_mm, lobby);
    if (!j->call) { j->state = J_FAILED; snprintf(j->err, sizeof j->err, "JoinLobby failed"); }
    UNLOCK();
    return j;
}

dsnet_steam_join *dsnet_steam_join_host(uint64_t host_steamid) {
    dsnet_steam_join *j = join_new();
    if (j) { LOCK(); j->host_id = host_steamid; UNLOCK(); }
    return j;
}

dsnet_steam_join *dsnet_steam_join_adopt(uint32_t hconn) {
    dsnet_steam_join *j = join_new();
    if (!j) return NULL;
    LOCK();
    uint16_t port = 0;
    int lfd = loopback_listen(&port);
    bridge *b = lfd >= 0 ? bridge_new(hconn, -1, lfd) : NULL;
    if (b) { b->join = j; j->br = b; j->port = port; j->state = J_READY; }
    else { if (lfd >= 0) close(lfd); j->state = J_FAILED; snprintf(j->err, sizeof j->err, "no bridge"); }
    UNLOCK();
    return j;
}

int dsnet_steam_join_poll(dsnet_steam_join *j, uint16_t *port) {
    LOCK();
    int r = j->state == J_READY ? 1 : j->state == J_FAILED ? -1 : 0;   /* J_LOBBY / J_CONNECTING: pending */
    if (r == 1 && port) *port = j->port;
    UNLOCK();
    return r;
}
const char *dsnet_steam_join_error(dsnet_steam_join *j) { return j->err; }

void dsnet_steam_join_close(dsnet_steam_join *j) {
    if (!j) return;
    LOCK();
    if (j->br) bridge_close(j->br);
    for (int i = 0; i < 16; i++) if (G_joins[i] == j) G_joins[i] = NULL;
    if (j->lobby) S.LeaveLobby(G_mm, j->lobby);
    UNLOCK();
    free(j);
    users_release();
}

uint64_t dsnet_steam_take_join_request(void) {
    LOCK();
    if (!G_args_checked && S.lib) {
        G_args_checked = 1;
        int argc = *_NSGetArgc(); char **argv = *_NSGetArgv();
        for (int i = 0; i + 1 < argc; i++)
            if (!strcmp(argv[i], "+connect_lobby")) G_join_req = strtoull(argv[i + 1], NULL, 10);
        char cl[512] = "";
        if (!G_join_req && S.SteamApps() && S.GetLaunchCommandLine(S.SteamApps(), cl, sizeof cl) > 0) parse_connect(cl);
    }
    uint64_t r = G_join_req; G_join_req = 0;
    UNLOCK();
    return r;
}

int dsnet_steam_friends_in_game(dsnet_steam_friend *out, int max) {
    if (!S.lib) return 0;
    I f = S.SteamFriends();
    uint32_t app = S.GetAppID(S.SteamUtils());
    int n = 0, cnt = S.GetFriendCount(f, k_EFriendFlagImmediate);
    for (int i = 0; i < cnt && n < max; i++) {
        uint64_t id = S.GetFriendByIndex(f, i, k_EFriendFlagImmediate);
        friend_game_info gi; memset(&gi, 0, sizeof gi);
        if (!S.GetFriendGamePlayed(f, id, &gi) || (uint32_t)(gi.game_id & 0xFFFFFF) != app || !gi.lobby) continue;
        out[n].steamid = id; out[n].lobby = gi.lobby;
        snprintf(out[n].name, sizeof out[n].name, "%s", S.GetFriendPersonaName(f, id));
        n++;
    }
    return n;
}

const char *dsnet_steam_lobby_data(uint64_t lobby, const char *key) {
    return S.lib ? S.GetLobbyData(S.SteamMatchmaking(), lobby, key) : "";
}

uint64_t dsnet_steam_lobby_owner(uint64_t lobby) {
    return S.lib && lobby ? S.GetLobbyOwner(S.SteamMatchmaking(), lobby) : 0;
}

int dsnet_steam_socket_pair(uint32_t *a, uint32_t *b) {
    return S.lib && S.CreateSocketPair(S.SNS(), a, b, false, NULL, NULL) ? 0 : -1;
}

/* ---- native transport registry hook (not in the core yet) ----
 * The bridge above needs no core change. To drop the loopback hop, the core needs two entry
 * points that take a transport where today they take an fd:
 *   dsnet_server.c: `int dsnet_server_add_transport(dsnet_server *s, dsnet_transport tr);`
 *                   (queue like pending_local, then add_conn with tr and local = 0)
 *   dsnet_client.c: `dsnet_client *dsnet_client_connect_transport(dsnet_transport tr, const char *name);`
 *                   (client_new with tr instead of dsnet_fd_transport(fd))
 * Then: accepted SNS connection -> dsnet_server_add_transport(s, dsnet_steam_transport(conn)). */
