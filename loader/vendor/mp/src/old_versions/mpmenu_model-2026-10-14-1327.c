// mpmenu_model.c - Multiplayer menu state machine, no game code (tests: mpmenu_test/).
#define MPM_NO_GAME
#include "mpmenu.h"
#include <stdio.h>
#include <string.h>

/* short labels (ux #17): at the stock button size 22 (design #8) the host page buttons fit on one line at 960 and 1512 */
void mpm_settings_summary(const mpm_settings *s, char *out, size_t cap) {
    static const char *ev[] = { "owner", "host", "first", "majority" }, *po[] = { "shared", "per player", "split", "multiplied" };
    snprintf(out, cap, "Pause: %s \xC2\xB7 Speed-up: %s \xC2\xB7 Gear: %s \xC2\xB7 Units: %d \xC2\xB7 Population: %s \xC2\xB7 Events: %s \xC2\xB7 Admin: %s", mpm_pause_text(s->pause_mode),
             s->speedup ? "on" : "off", mpm_gear_text(s->gear), s->units, po[s->population >= 0 && s->population < 4 ? s->population : 0],
             ev[s->events >= 0 && s->events < 4 ? s->events : 0], s->admin ? "on" : "off");
}
const char *mpm_population_desc(int mode) {           /* owner's wording (lead 2026-10-13) */
    return mode == 1 ? "Each player can recruit up to the clan limit for himself"
         : mode == 2 ? "The clan limit is divided between the players"
         : mode == 3 ? "Clan limit x players connected, shared by all" : "One clan limit for everyone, first come";
}
const char *mpm_rule_tip(int row) {                     /* tooltip bodies (research/tooltips_v9.md 3.3, uxdesign) */
    static const char *t[MPM_RULE_ROWS] = {
        "Who can pause the game. Anyone can pause: one player pauses it for everyone. Pause by vote: it pauses only when every player votes for it. No pause: nobody can pause it. Events, the Portal screen and a lost player still pause in every mode.",
        "On: players can play at Fast speed when every player asks for it. Off: the game stays at normal speed for everyone.",
        "Who may change gear. Shared, own units: everyone uses the settlement storage, and each player equips only their own units. Shared, any unit: anyone equips any unit. Own storage: each player has their own items.",
        "The most units each player can control at the same time (1, 2, 3, 4 or 6). The host can change it during play.",
        "How the clan's population limit is shared. Shared: one limit for everyone, first come. Per player: each player has the full limit. Split equally: the limit is divided between players. Multiplied: limit times players, shared. A hire needs room here and under Units per Player.",
        "Who answers an event. Owner of the unit: the player who controls the unit in the event. Host only. First to answer. Majority vote: the host breaks a tie.",
        "On: the host can order every unit and release the units of absent players. Off: the host is a normal player for other players' units." };
    return row >= 0 && row < MPM_RULE_ROWS ? t[row] : "";
}
const char *mpm_rule_tip_title(int row) {
    static const char *t[MPM_RULE_ROWS] = { "Pause", "Speed-up", "Gear", "Units per Player", "Population", "Event Choices", "Admin Mode" };
    return row >= 0 && row < MPM_RULE_ROWS ? t[row] : "";
}
const char *mpm_rule_label(int row) {
    static const char *l[MPM_RULE_ROWS] = { "Pause", "Speed-up", "Gear", "Units per player", "Population", "Event choices", "Admin mode" };
    return row >= 0 && row < MPM_RULE_ROWS ? l[row] : "";
}
void mpm_rule_value(const mpm_settings *s, int row, char *out, size_t cap) {
    switch (row) {
    case 0: snprintf(out, cap, "%s", s->pause_mode == MPM_PAUSE_NONE ? "No pause" : s->pause_mode == MPM_PAUSE_ALL_VOTE ? "Pause by vote" : "Anyone can pause"); break;
    case 1: snprintf(out, cap, "%s", s->speedup ? "On" : "Off"); break;
    case 2: snprintf(out, cap, "%s", s->gear == MPM_GEAR_SHARED_ANY ? "Shared, any unit" : s->gear == MPM_GEAR_OWN_STORAGE ? "Own storage" : "Shared, own units"); break;
    case 3: snprintf(out, cap, "%d", s->units); break;
    case 4: snprintf(out, cap, "%s", s->population == 1 ? "Per player" : s->population == 2 ? "Split equally" : s->population == 3 ? "Multiplied" : "Shared"); break;
    case 5: snprintf(out, cap, "%s", s->events == 1 ? "Host only" : s->events == 2 ? "First to answer" : s->events == 3 ? "Majority vote" : "Owner of the unit"); break;
    case 6: snprintf(out, cap, "%s", s->admin ? "On" : "Off"); break;
    default: out[0] = 0;
    }
}
const char *mpm_sel_value(const mpm_model *m, int sel) {
    return sel == MPB_SEL_PAUSE ? mpm_pause_text(m->set.pause_mode) : sel == MPB_SEL_SPEED ? (m->set.speedup ? "on" : "off")
         : sel == MPB_SEL_GEAR ? mpm_gear_text(m->set.gear) : sel == MPB_SEL_UNITS ? (const char *[]){ "?", "1", "2", "3", "4", "?", "6" }[m->set.units >= 1 && m->set.units <= 6 ? m->set.units : 0] : "";
}
const char *mpm_pause_text(int mode) {
    return mode == MPM_PAUSE_NONE ? "off" : mode == MPM_PAUSE_ALL_VOTE ? "by vote" : "anyone";
}
const char *mpm_gear_text(int gear) {
    return gear == MPM_GEAR_SHARED_ANY ? "any unit" : gear == MPM_GEAR_OWN_STORAGE ? "own storage" : "own units";
}

void mpm_init(mpm_model *m) {
    memset(m, 0, sizeof *m);
    m->state = MPM_IDLE;
    m->set.pause_mode = MPM_PAUSE_ANYONE; m->set.speedup = 1; m->set.gear = MPM_GEAR_SHARED_OWN; m->set.units = 3;
}

static int go(mpm_model *m, int st) { m->state = st; return MPA_REFRESH; }
static const mpm_obs *cur;
static int fail(mpm_model *m, const char *why) { snprintf(m->msg, sizeof m->msg, "%s", cur && cur->msg[0] ? cur->msg : why); return go(m, MPM_FAILED); }
/* the session ended without our own Leave: a stock notice box on the title says why (ux #9), not the button line */
static int ended(mpm_model *m, const mpm_obs *o) {
    m->msg[0] = 0;
    if (!strcmp(o->msg, "The host left the game")) {
        const char *h = m->host[0] ? m->host : "The host";
        if (m->host[0]) snprintf(m->notice, sizeof m->notice, "<size=150%%>%s left the game</size>\nThe world is saved on %s's computer. To play again, %s hosts it and invites you.", h, h, h);
        else snprintf(m->notice, sizeof m->notice, "<size=150%%>The host left the game</size>\nThe world is saved on the host's computer. To play again, the host hosts it and invites you.");
    } else if (!strcmp(o->msg, "Connection to the host lost")) {           /* our own link dropped (no host BYE): the host waits */
        if (m->host[0]) snprintf(m->notice, sizeof m->notice, "<size=150%%>Connection to %s lost</size>\n%s is waiting for you: rejoin from Recent worlds.", m->host, m->host);
        else snprintf(m->notice, sizeof m->notice, "<size=150%%>Connection to the host lost</size>\nThe host is waiting for you: rejoin from Recent worlds.");
    } else snprintf(m->notice, sizeof m->notice, "%s", o->msg);
    return go(m, MPM_IDLE);
}

/* selector arrows (host page and lobby): previous / next value of a rule; 1 if it changed */
static const int units_list[] = { 1, 2, 3, 4, 6 };
static int units_step(int u, int d) {
    int i = 0; while (i < 5 && units_list[i] != u) i++;
    if (i == 5) i = 2;
    return units_list[(i + d + 5) % 5];
}
static int rule_arrow(mpm_model *m, int c) {
    switch (c) {
    case MPB_PAUSE_PREV: m->set.pause_mode = (m->set.pause_mode + 2) % 3; return 1;
    case MPB_PAUSE_NEXT: case MPB_PAUSE_MODE: m->set.pause_mode = (m->set.pause_mode + 1) % 3; return 1;
    case MPB_SPEED_PREV: case MPB_SPEED_NEXT: case MPB_SPEEDUP: m->set.speedup = !m->set.speedup; return 1;   /* two values */
    case MPB_GEAR_PREV: m->set.gear = (m->set.gear + 2) % 3; return 1;
    case MPB_GEAR_NEXT: case MPB_GEAR: m->set.gear = (m->set.gear + 1) % 3; return 1;
    case MPB_UNITS_PREV: m->set.units = units_step(m->set.units, -1); return 1;
    case MPB_UNITS_NEXT: m->set.units = units_step(m->set.units, 1); return 1;
    case MPB_ADMIN_PREV: case MPB_ADMIN_NEXT: m->set.admin = !m->set.admin; return 1;
    case MPB_EVENTS_PREV: m->set.events = (m->set.events + 3) % 4; return 1;
    case MPB_EVENTS_NEXT: m->set.events = (m->set.events + 1) % 4; return 1;
    case MPB_POP_PREV: m->set.population = (m->set.population + 3) % 4; return 1;
    case MPB_POP_NEXT: m->set.population = (m->set.population + 1) % 4; return 1;
    }
    return 0;
}
int mpm_step(mpm_model *m, const mpm_obs *o, int c) {
    int a = 0; cur = o;
    m->steam = o->steam;
    /* Escape = Back on every title page (the Rules panel closes first) */
    if (o->escape && c == MPB_NONE && o->scene == 1) {
        if (m->state == MPM_TITLE_HOST && m->settings_open) c = MPB_RULES_DONE;
        else if (m->state == MPM_TITLE_MENU || m->state == MPM_TITLE_HOST || m->state == MPM_TITLE_FRIENDS) c = MPB_BACK;
        else if (m->state == MPM_TITLE_JOIN) c = m->connecting[0] ? MPB_CANCEL_JOIN : MPB_BACK;
    }
    if (o->escape && c == MPB_NONE && m->state == MPM_LOBBY && m->lobby_settings_open) c = MPB_RULES_DONE;   /* lobby: Escape closes the Rules */
    if (o->host_name[0]) snprintf(m->host, sizeof m->host, "%s", o->host_name);
    switch (m->state) {
    case MPM_IDLE: case MPM_FAILED:
        if (o->role == 2) { m->msg[0] = 0; return go(m, MPM_GUEST_JOINING); }
        if (o->role == 1 && o->scene == 2) return go(m, MPM_PLAYING);          /* a session started another way (mp.json) */
        if (c == MPB_MULTIPLAYER && o->scene == 1) { m->msg[0] = 0; return go(m, MPM_TITLE_MENU); }
        if (o->esc_open && c == MPB_NONE) return 0;
        break;
    case MPM_TITLE_MENU:
        if (o->role == 2) return go(m, MPM_GUEST_JOINING);
        if (o->scene != 1) return go(m, MPM_IDLE);
        if (c == MPB_HOST) return go(m, MPM_TITLE_HOST);
        if (c == MPB_JOIN_ADDR) { m->msg[0] = 0; return go(m, MPM_TITLE_JOIN); }
        if (c >= MPB_HIST0 && c <= MPB_HIST5 && m->hist_page * 6 + (c - MPB_HIST0) < m->nhist) { m->hist_pick = m->hist_page * 6 + (c - MPB_HIST0); m->msg[0] = 0; return MPA_HIST_JOIN | MPA_REFRESH; }
        if (c >= MPB_HISTX0 && c <= MPB_HISTX5 && m->hist_page * 6 + (c - MPB_HISTX0) < m->nhist) { m->hist_pick = m->hist_page * 6 + (c - MPB_HISTX0); return MPA_HIST_DEL | MPA_REFRESH; }
        if (c == MPB_HIST_PREV && m->hist_page > 0) { m->hist_page--; return MPA_REFRESH; }
        if (c == MPB_HIST_NEXT && (m->hist_page + 1) * 6 < m->nhist) { m->hist_page++; return MPA_REFRESH; }
        if (c == MPB_JOIN_FRIEND && o->steam) { m->msg[0] = 0; m->nfriends = 0; return go(m, MPM_TITLE_FRIENDS) | MPA_FRIENDS; }   /* LAN app: greyed, no action */
        if (c == MPB_BACK) { a = m->connecting[0] ? MPA_JOIN_CANCEL : 0; m->connecting[0] = 0; m->msg[0] = 0; return go(m, MPM_IDLE) | a; }
        break;
    case MPM_TITLE_FRIENDS:
        if (o->role == 2) { m->msg[0] = 0; return go(m, MPM_GUEST_JOINING); }
        if (o->scene != 1) return go(m, MPM_IDLE);
        if (c >= MPB_FRIEND0 && c <= MPB_FRIEND5 && c - MPB_FRIEND0 < m->nfriends) { m->friend_pick = c - MPB_FRIEND0; return MPA_JOIN_FRIEND | MPA_REFRESH; }
        if (c == MPB_BACK) return go(m, MPM_TITLE_MENU);
        break;
    case MPM_TITLE_JOIN:
        if (o->role == 2) { m->msg[0] = m->connecting[0] = 0; return go(m, MPM_GUEST_JOINING); }
        if (o->scene != 1) { a = m->connecting[0] ? MPA_JOIN_CANCEL : 0; m->connecting[0] = 0; return go(m, MPM_IDLE) | a; }
        if (o->edited && !m->connecting[0]) { m->msg[0] = 0; a |= MPA_REFRESH; }   /* an old error goes when the text changes */
        if (c == MPB_CONNECT && !m->connecting[0]) { m->msg[0] = 0; return MPA_JOIN_ADDR | MPA_REFRESH; }   /* game side sets connecting or msg */
        if (c == MPB_CANCEL_JOIN && m->connecting[0]) { m->connecting[0] = 0; m->msg[0] = 0; return MPA_JOIN_CANCEL | MPA_REFRESH; }
        if (c == MPB_BACK) { a = m->connecting[0] ? MPA_JOIN_CANCEL : 0; m->msg[0] = m->connecting[0] = 0; return go(m, MPM_TITLE_MENU) | a; }
        break;
    case MPM_TITLE_HOST:
        if (o->role == 2) return go(m, MPM_GUEST_JOINING);
        if (o->scene != 1) return go(m, MPM_IDLE);
        if (o->edited) m->msg[0] = 0;
        if (c == MPB_SETTINGS) { m->settings_open = !m->settings_open; return MPA_REFRESH; }
        if (c == MPB_RULES_DONE) { m->settings_open = 0; return MPA_REFRESH; }
        if (rule_arrow(m, c)) return MPA_REFRESH;
        if (c == MPB_PAUSE_MODE) { m->set.pause_mode = (m->set.pause_mode + 1) % 3; return MPA_REFRESH; }
        if (c == MPB_SPEEDUP) { m->set.speedup = !m->set.speedup; return MPA_REFRESH; }
        if (c == MPB_GEAR) { m->set.gear = (m->set.gear + 1) % 3; return MPA_REFRESH; }
        if (c == MPB_LOAD_SAVE) { m->picker_seen = 0; m->closed_frames = 0; return go(m, MPM_PICK_SAVE) | MPA_OPEN_LOAD_LIST; }
        if (c == MPB_NEW_CAMPAIGN) { m->picker_seen = 0; m->closed_frames = 0; return go(m, MPM_PICK_NEW) | MPA_OPEN_NEW; }
        if (c == MPB_BACK) return go(m, MPM_TITLE_MENU);
        break;
    case MPM_PICK_SAVE: case MPM_PICK_NEW:
        if (o->scene != 1 || o->loading) { m->closed_frames = 0; return go(m, MPM_HOST_LOADING); }     /* the campaign is loading */
        if (o->picker_open || !o->title_visible) { m->picker_seen = 1; m->closed_frames = 0; }
        else if (m->picker_seen && ++m->closed_frames > 90) return go(m, MPM_TITLE_HOST);   /* closed, nothing loads: back */
        break;
    case MPM_HOST_LOADING:
        if (o->campaign_ready) { m->closed_frames = 0; return go(m, MPM_LOBBY) | MPA_HOST_START | MPA_LOBBY_SHOW; }
        if (o->scene == 1 && o->title_visible && !o->loading && ++m->closed_frames > 300) return fail(m, "Loading the campaign failed");
        break;
    case MPM_LOBBY:
        if (o->scene == 1 && o->title_visible && !o->loading) {              /* back on the title (quit): end the session */
            if (++m->closed_frames > 90) { m->closed_frames = 0; return go(m, MPM_IDLE) | MPA_LEAVE | MPA_LOBBY_HIDE; }
        } else m->closed_frames = 0;
        if (o->failed) return fail(m, "Multiplayer could not start") | MPA_LOBBY_HIDE;
        if (o->role == 0 && c == MPB_NONE) { snprintf(m->msg, sizeof m->msg, "%s", o->msg); return go(m, MPM_IDLE) | MPA_LOBBY_HIDE; }
        if (c == MPB_INVITE) return o->steam ? MPA_INVITE : 0;            /* LAN app: greyed "Needs the Steam version" */
        if (c == MPB_COPY_ADDR) return MPA_COPY_ADDR;
        if (c == MPB_START) return go(m, MPM_PLAYING) | MPA_LOBBY_HIDE | MPA_START_GAME;
        if (c == MPB_LOBBY_SETTINGS) { m->lobby_settings_open = !m->lobby_settings_open; return MPA_REFRESH; }
        if (c == MPB_ADMIN || ((c == MPB_ADMIN_PREV || c == MPB_ADMIN_NEXT) && m->lobby_settings_open)) return MPA_ADMIN | MPA_REFRESH;   /* dsnet admin mode */
        if (c == MPB_RULES_DONE) { m->lobby_settings_open = 0; return MPA_REFRESH; }
        if (m->lobby_settings_open && rule_arrow(m, c)) return MPA_RULES | MPA_REFRESH;   /* host changes the rules in the lobby */
        if (c == MPB_LEAVE) return go(m, MPM_IDLE) | MPA_LEAVE | MPA_LOBBY_HIDE;
        break;
    case MPM_PLAYING:
        if (o->scene == 1 && o->title_visible && !o->loading) {              /* back on the title (quit): end the session */
            if (++m->closed_frames > 90) { m->closed_frames = 0; return go(m, MPM_IDLE) | MPA_LEAVE; }
        } else m->closed_frames = 0;
        if (c == MPB_ESC_INVITE && o->role == 1) return MPA_INVITE;
        if (c == MPB_LEAVE) { m->msg[0] = 0; return go(m, MPM_IDLE) | MPA_LEAVE; }
        if (o->role == 0) {                                                  /* steamnet's reason ("The host left the game"); own quit: none */
            if (!o->msg[0]) { m->msg[0] = 0; return go(m, MPM_IDLE); }
            return ended(m, o);
        }
        if (o->role == 2 && o->host_lobby && o->campaign_ready) return go(m, MPM_GUEST_LOBBY) | MPA_LOBBY_SHOW;
        break;
    case MPM_GUEST_LOBBY:                                                    /* host still in the lobby: waiting panel */
        if (o->scene == 1 && o->title_visible && !o->loading) {
            if (++m->closed_frames > 90) { m->closed_frames = 0; return go(m, MPM_IDLE) | MPA_LEAVE | MPA_LOBBY_HIDE; }
        } else m->closed_frames = 0;
        if (c == MPB_LEAVE) { m->msg[0] = 0; return go(m, MPM_IDLE) | MPA_LEAVE | MPA_LOBBY_HIDE; }
        if (o->role == 0) return (o->msg[0] ? ended(m, o) : go(m, MPM_IDLE)) | MPA_LOBBY_HIDE;
        if (!o->host_lobby) return go(m, MPM_PLAYING) | MPA_LOBBY_HIDE;
        break;
    case MPM_GUEST_JOINING: case MPM_GUEST_LOADING:
        if (c == MPB_CANCEL_JOIN) return go(m, MPM_IDLE) | MPA_LEAVE;
        if (o->failed || o->role == 0) return fail(m, "Could not join the game");
        if (o->guest_loading && m->state == MPM_GUEST_JOINING) a |= go(m, MPM_GUEST_LOADING);
        if (o->campaign_ready && !o->guest_loading) { m->closed_frames = 0; return go(m, MPM_PLAYING); }
        break;
    }
    return a;
}

uint64_t mpm_visible(const mpm_model *m, const mpm_obs *o) {
#define B(x) (1ull << (x))
    switch (m->state) {
    case MPM_IDLE: case MPM_FAILED:
        return o->scene == 1 ? B(MPB_MULTIPLAYER) : 0;
    case MPM_TITLE_MENU: return B(MPB_HOST) | B(MPB_JOIN_ADDR) | B(MPB_JOIN_FRIEND) | B(MPB_BACK) | (m->msg[0] || m->connecting[0] ? B(MPB_INFO) : 0);   /* INFO: reconnect status */
    case MPM_TITLE_FRIENDS: {
        uint64_t v = B(MPB_HEADER) | B(MPB_INFO) | B(MPB_DIVIDER) | B(MPB_BACK);
        for (int i = 0; i < m->nfriends && i < 6; i++) v |= B(MPB_FRIEND0 + i);
        return v;
    }
    case MPM_TITLE_JOIN:                                                     /* INFO = connecting / error / no-field hint */
        return B(MPB_HEADER) | B(MPB_LABEL_ADDR) | B(MPB_LABEL_NAME) | B(MPB_DIVIDER) | (m->connecting[0] ? B(MPB_CANCEL_JOIN) : B(MPB_CONNECT)) | B(MPB_BACK)
             | (m->connecting[0] || m->msg[0] || m->no_field ? B(MPB_INFO) : 0);
    case MPM_TITLE_HOST:                                                     /* layout v2 §2a */
        return B(MPB_HEADER) | B(MPB_LABEL_NAME) | (m->msg[0] ? B(MPB_INFO) : 0) | B(MPB_SETTINGS)
             /* the Rules panel (settings_open) is drawn by the game side next to the column (approved mockup B) */
             | B(MPB_DIVIDER) | B(MPB_LOAD_SAVE) | B(MPB_NEW_CAMPAIGN) | B(MPB_BACK);
    case MPM_LOBBY: return B(MPB_INVITE) | B(MPB_COPY_ADDR) | B(MPB_START) | B(MPB_LEAVE);
    case MPM_GUEST_LOBBY: return B(MPB_LEAVE);
    case MPM_PLAYING: return o->esc_open ? ((o->role == 1 ? B(MPB_ESC_INVITE) : 0) | B(MPB_LEAVE)) : 0;
    case MPM_GUEST_JOINING: case MPM_GUEST_LOADING:
        return o->scene == 1 ? B(MPB_INFO) | B(MPB_CANCEL_JOIN) : 0;          /* INFO = status line */
    }
    return 0;
#undef B
}

void mpm_label(const mpm_model *m, int b, char *out, size_t cap) {
    switch (b) {
    case MPB_MULTIPLAYER:
        if (m->msg[0]) snprintf(out, cap, "Multiplayer <size=60%%>(%s)</size>", m->msg);
        else snprintf(out, cap, "Multiplayer");
        break;
    case MPB_LABEL_ADDR: snprintf(out, cap, "Host address (ip:port)"); break;
    case MPB_HEADER: snprintf(out, cap, "%s", m->state == MPM_TITLE_JOIN ? "Join a multiplayer world" : m->state == MPM_TITLE_FRIENDS ? "Join a friend on Steam" : "Host a multiplayer world"); break;
    case MPB_JOIN_FRIEND: snprintf(out, cap, "%s", m->steam ? "Join a friend on Steam" : "<color=#A69085><line-height=80%>Join a friend on Steam\n<size=65%>Needs the Steam version</size></line-height></color>"); break;
    case MPB_FRIEND0: case MPB_FRIEND1: case MPB_FRIEND2: case MPB_FRIEND3: case MPB_FRIEND4: case MPB_FRIEND5:
        snprintf(out, cap, "Join %s's world", m->friends[b - MPB_FRIEND0][0] ? m->friends[b - MPB_FRIEND0] : "a friend"); break;
    case MPB_SETTINGS: snprintf(out, cap, "Rules"); break;                  /* cog left, "›" right, summary under it */
    case MPB_SUMMARY: mpm_settings_summary(&m->set, out, cap); break;
    case MPB_SEL_PAUSE: snprintf(out, cap, "Pause"); break;
    case MPB_SEL_SPEED: snprintf(out, cap, "Speed-up"); break;
    case MPB_SEL_GEAR: snprintf(out, cap, "Gear"); break;
    case MPB_SEL_UNITS: snprintf(out, cap, "Units per player"); break;
    case MPB_UNITS_PREV: case MPB_UNITS_NEXT: out[0] = 0; break;
    case MPB_DIVIDER: case MPB_PAUSE_PREV: case MPB_PAUSE_NEXT: case MPB_SPEED_PREV: case MPB_SPEED_NEXT:
    case MPB_GEAR_PREV: case MPB_GEAR_NEXT: out[0] = 0; break;
    case MPB_LABEL_NAME: snprintf(out, cap, "Your name"); break;
    case MPB_INFO:                                                          /* small type, errors in red */
        if (m->state == MPM_TITLE_FRIENDS)
            snprintf(out, cap, "%s", m->msg[0] ? m->msg : m->nfriends ? "Friends hosting a world now:" : "No Steam friend is hosting right now. Ask them to host, or use Join by address.");
        else if (m->state == MPM_GUEST_JOINING || m->state == MPM_GUEST_LOADING)
            snprintf(out, cap, "<size=75%%>Joining %s's game...%s</size>", m->host[0] ? m->host : "the host", m->state == MPM_GUEST_LOADING ? " loading" : "");
        else if (m->connecting[0]) snprintf(out, cap, "<size=70%%>Connecting to %s...</size>", m->connecting);
        else if (m->msg[0]) snprintf(out, cap, "<color=#F16071>%s</color>", m->msg);   /* Lora 14 (game side) */
        else if (m->no_field) snprintf(out, cap, "<size=60%%>Copy the host's address (ip:port), then press Connect</size>");
        else out[0] = 0;
        break;
    case MPB_HOST: snprintf(out, cap, "Host a game"); break;
    case MPB_BACK: snprintf(out, cap, "Back"); break;
    case MPB_PAUSE_MODE: snprintf(out, cap, "Pause: %s", mpm_pause_text(m->set.pause_mode)); break;
    case MPB_SPEEDUP: snprintf(out, cap, "Speed-up: %s", m->set.speedup ? "on" : "off"); break;
    case MPB_GEAR: snprintf(out, cap, "Gear: %s", mpm_gear_text(m->set.gear)); break;
    case MPB_LOAD_SAVE: snprintf(out, cap, "Host from a save"); break;
    case MPB_NEW_CAMPAIGN: snprintf(out, cap, "Host a new campaign"); break;
    case MPB_INVITE: case MPB_ESC_INVITE: snprintf(out, cap, "Steam invite"); break;
    case MPB_JOIN_ADDR: snprintf(out, cap, "Join by address"); break;
    case MPB_CONNECT: snprintf(out, cap, "%s", m->no_field ? "Connect (address from the clipboard)" : "Connect"); break;
    case MPB_COPY_ADDR: snprintf(out, cap, "Copy direct address"); break;
    case MPB_START: snprintf(out, cap, "Start"); break;
    case MPB_LEAVE: snprintf(out, cap, "Leave multiplayer"); break;
    case MPB_CANCEL_JOIN: snprintf(out, cap, "Cancel"); break;
    default: snprintf(out, cap, "?"); break;
    }
}

/* lobby panel (design #6): a title line (Cinzel, set by the game side) and a body: next step, one line per player ("Â· name": Lora has no U+25CF),
 * settings in the secondary colour, addresses. No <b> (TMP swaps in a font that is not the game's). */
void mpm_lobby_title(const mpm_model *m, char *out, size_t cap) {
    if (m->state == MPM_GUEST_LOBBY) snprintf(out, cap, "In %s's lobby", m->host[0] ? m->host : "the host");
    else snprintf(out, cap, "Multiplayer lobby");
}
void mpm_lobby_text(const mpm_model *m, const char *const *names, int n, char *out, size_t cap) {
    size_t o;
    if (m->state == MPM_GUEST_LOBBY) {                                     /* guest waiting panel */
        const char *h = m->host[0] ? m->host : "the host";
        o = (size_t)snprintf(out, cap, "The game starts when %s presses Start.", h);
    } else o = (size_t)snprintf(out, cap, "Share your address, then press Start when everyone is in.");
    if (n <= 0 && o < cap) o += (size_t)snprintf(out + o, cap - o, "\n\xC2\xB7 You (host)");
    for (int i = 0; i < n && o < cap; i++)
        o += (size_t)snprintf(out + o, cap - o, "\n\xC2\xB7 %s%s", names[i] && names[i][0] ? names[i] : "Player", i == 0 ? " (host)" : "");
    /* guest: no settings line (m->set holds this copy's own host settings, not the host's) */
    if (o < cap && m->state != MPM_GUEST_LOBBY)
        o += (size_t)snprintf(out + o, cap - o, "\n<size=87%%><color=#A69085>Pause: %s  \xC2\xB7  Speed-up: %s  \xC2\xB7  Gear: %s</color></size>",
                              mpm_pause_text(m->set.pause_mode), m->set.speedup ? "on" : "off", mpm_gear_text(m->set.gear));
    if (o < cap && m->direct[0] && m->state != MPM_GUEST_LOBBY) {
        const char *port = strrchr(m->direct, ':');
        o += (size_t)snprintf(out + o, cap - o, "\nLocal: %s", m->direct);
        if (o < cap && m->internet[0]) o += (size_t)snprintf(out + o, cap - o, "\nInternet: %s", m->internet);
        if (o < cap) snprintf(out + o, cap - o, "\nFriends outside your home: open port %s on your router, or use Steam invite", port ? port + 1 : "47777");
    }
}
