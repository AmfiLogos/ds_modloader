// Test-only module (never shipped): measures the real global cooldown of units in game.
// SkillComponent (build 25846389): +0x60 _globalCooldown (remaining), +0x64 _globalCooldownDuration (set value),
// +0x68 _globalCooldownValue (combat table), +0x6C _changeEquipCooldownValue. Disassembly 0xDDBA18..0xDDBA94:
// SetGlobalCooldown() / (float) / SetChangeEquipCooldown() store the new value in +0x60 and +0x64 (only when longer);
// SubtractGlobalCooldown(dt) lowers +0x60 and zeroes both at <= 0. Tick_Skill (0xA977A8) calls HasGlobalCooldown()
// every tick and counts down through TickEventApplier.Apply(GlobalCooldownApplyData(-dt)) -> SubtractGlobalCooldown.
//   hook SubtractGlobalCooldown: a cooldown is measured from the tick it first counts down until it reaches 0, as the
//        sum of the game's own dt (game seconds) -> "lasted".
//   hook HasGlobalCooldown: census of units (cooldown reduction, stat 28) and, with DSL_GCDPROBE_FORCE=N, N times
//        starts a global cooldown on a unit with cooldown reduction through the game's public SetGlobalCooldown()
//        (the call TickEventApplier makes after a skill; gcd-cdr hooks exactly that function).
// Results: lines "GCD ..." in dsload.log.
#include "dsl_sdk.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

DSL_PLUGIN();
#define STAT_CDR 28
static const DSLHost *H; static DSLPlugin *ME;
static void (*orig_sub)(void *self, float dt, const void *mi);
static int (*orig_has)(void *self, const void *mi);
static void (*set_gcd)(void *self, const void *mi);
static void *(*get_component)(void *entity, const void *mi);
static float (*get_value)(void *reader, int32_t stat, const void *mi);
static void (*init_md)(void *slot_addr);
static void **mi_slot;

#define F(s, o) (*(float *)((char *)(s) + (o)))
typedef struct { void *sc; float cdr; int active, kind, ticks, forced; float start, acc, base; uint64_t next_ok; } St;
static St T[512]; static int nt;
static int force_left, forced_total, measured;
static uint64_t has_calls;

static float cdr_of(void *sc) {
    void *owner = *(void **)((char *)sc + 0x10);
    if (!owner) return -1;
    init_md(mi_slot);
    void *r = get_component(owner, *mi_slot);
    return r ? get_value(r, STAT_CDR, NULL) : -1;
}
static St *st_of(void *sc) {
    for (int i = 0; i < nt; i++) if (T[i].sc == sc) return &T[i];
    if (nt >= 512) return NULL;
    St *s = &T[nt++]; memset(s, 0, sizeof *s); s->sc = sc; s->cdr = cdr_of(sc);
    if (nt <= 60) H->log(ME, "GCD census: unit #%d cooldown reduction %.2f, combat-table GCD %.3f s", nt, s->cdr, F(sc, 0x68));
    return s;
}

static void my_sub(void *self, float dt, const void *mi) {
    St *s = self ? st_of(self) : NULL;
    if (s && !s->active && F(self, 0x60) > 0) {
        s->active = 1; s->start = F(self, 0x64); s->acc = 0; s->ticks = 0; s->base = F(self, 0x68); s->cdr = cdr_of(self);
        s->kind = (s->start == F(self, 0x6C) && s->start != s->base) ? 2 : 1;          /* 2 = equipment swap */
    }
    orig_sub(self, dt, mi);
    if (s && s->active) {
        s->acc += dt; s->ticks++;
        if (F(self, 0x60) <= 0) {
            s->active = 0; measured++;
            float expect_mod = s->base / (1.0f + s->cdr * 0.01f);
            H->log(ME, "GCD measured #%d (%s%s): started at %.3f s, lasted %.3f game s over %d ticks (dt %.4f); "
                   "cooldown reduction %.2f; stock would be %.3f s, with the rule %.3f s",
                   measured, s->kind == 2 ? "equipment swap" : "global cooldown", s->forced ? ", forced" : ", from a skill",
                   s->start, s->acc, s->ticks, dt, s->cdr, s->base, expect_mod);
            s->forced = 0; s->next_ok = has_calls + 3000;
        }
    }
}

static int my_has(void *self, const void *mi) {
    has_calls++;
    if (self && force_left > 0 && F(self, 0x60) <= 0) {
        St *s = st_of(self);
        if (s && s->cdr > 0 && !s->active && !s->forced && has_calls >= s->next_ok) {
            force_left--; forced_total++; s->forced = 1;
            H->log(ME, "GCD force #%d: SetGlobalCooldown() on a unit with cooldown reduction %.2f (combat-table %.3f s)", forced_total, s->cdr, F(self, 0x68));
            set_gcd(self, NULL);
            H->log(ME, "GCD force #%d: game stored %.3f s", forced_total, F(self, 0x64));
        }
    }
    if (has_calls == 1 || has_calls % 200000 == 0) H->log(ME, "GCD probe: HasGlobalCooldown called %llu times, %d units seen, %d measured", (unsigned long long)has_calls, nt, measured);
    return orig_has(self, mi);
}

DSL_EXPORT int dsl_plugin_init(const DSLHost *host, DSLPlugin *self) {
    H = host; ME = self;
    const char *ns = "Refactor.Component", *cls = "SkillComponent";
    void *k = H->find_class(ns, cls);
    if (!k) return 2;
    if (H->field_offset(k, "_globalCooldown") != 0x60 || H->field_offset(k, "_globalCooldownDuration") != 0x64 ||
        H->field_offset(k, "_globalCooldownValue") != 0x68 || H->field_offset(k, "_changeEquipCooldownValue") != 0x6C ||
        H->field_offset(k, "_owner") != 0x10) { H->log(ME, "field layout differs: probe off"); return 3; }
    void *ms = H->find_method(k, "SetGlobalCooldown", 0);
    set_gcd = ms ? H->method_address(ms) : NULL;
    get_value = H->find_method_address("Refactor", "StatusReader", "GetValue", 1);
    get_component = H->table(ME, "EntityExtensions.GetComponent<object>");
    mi_slot = H->table(ME, "slot.GetComponent<StatusReader>");
    init_md = H->table(ME, "il2cpp_codegen_initialize_runtime_metadata");
    if (!set_gcd || !get_value || !get_component || !mi_slot || !init_md) { H->log(ME, "lookups missing"); return 4; }
    const char *f = getenv("DSL_GCDPROBE_FORCE"); force_left = f ? atoi(f) : 0;
    if (H->hook_method(ME, ns, cls, "SubtractGlobalCooldown", 1, (void *)my_sub, (void **)&orig_sub)) return 5;
    if (H->hook_method(ME, ns, cls, "HasGlobalCooldown", 0, (void *)my_has, (void **)&orig_has)) return 6;
    H->log(ME, "GCD probe up (force %d); gcd-cdr %s", force_left, H->module_loaded && H->module_loaded("gcd-cdr") ? "loaded" : "not loaded");
    return 0;
}
