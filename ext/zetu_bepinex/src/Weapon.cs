// skills_without_weapon (zetu.c parts 1 and 1b).
// The game checks a skill's RequiredEquipment ("Equipment_Blunt", ...) in ONE place for use (player and AI):
// Refactor.Combat.EquipmentCondition.CheckCondition looks through the equipment slots, and when none matches asks
// Refactor.SkillReader.HasEquipBypass(key) (the game's own bypass: Fire Battle Mage, Nature Spirit, Brick). Here
// HasEquipBypass answers yes for every "Equipment_*" key. Learning is never blocked by the weapon in the stock game; its
// "you need X, continue anyway?" text and the tooltip's red line ask the same bypass. Prerequisites, levels, skill points,
// ammo (arrows) and the weapon passives (Healing Touch, Two-Hand Grip, Shield Handling) stay stock.
// 1b. A cast with an EMPTY main hand: the iterator of DurabilityCondition.OnSuccessCondition (<OnSuccessCondition>d__N)
// yields Execution.AddStatus(<item in the slot>, Durability, -cost) with Target null, and the game's FX throws a
// NullReferenceException at every such cast. That one yield is dropped (nothing to wear); with an item in the slot
// nothing changes. Depends only on game state (same result on every peer). The iterator is found by its method name,
// never by its number (Plugin.IterType / IterField).
using System;
using System.Runtime.CompilerServices;
using HarmonyLib;
using Il2CppInterop.Runtime.InteropTypes;
using Refactor;
using Refactor.Combat;

namespace ZetuSuite
{
    static class Weapon
    {
        const string Iter = "OnSuccessCondition";                     // DurabilityCondition's iterator, by method name
        static int bypassN, noHandN, errN;

        internal static void Install()
        {
            var h = new Harmony(Plugin.Guid + ".weapon");
            var it = Plugin.IterType(typeof(DurabilityCondition), Iter);
            bool ok = Plugin.Patch(h, typeof(Weapon), typeof(SkillReader), nameof(SkillReader.HasEquipBypass), 1, prefix: nameof(BypassPrefix))
                   && it != null && Plugin.Patch(h, typeof(Weapon), it, "MoveNext", 0, postfix: nameof(DurabilityPostfix));
            if (!ok) { h.UnpatchSelf(); Plugin.L.LogError("skills without weapon requirement: OFF (a game method is missing)"); return; }
            Plugin.L.LogInfo("skills without weapon requirement: on (SkillReader.HasEquipBypass, DurabilityCondition iterator)");
        }

        static bool BypassPrefix(string key, ref bool __result)
        {
            if (key == null || key.Length <= 10 || !key.StartsWith("Equipment_", StringComparison.Ordinal)) return true;
            if (++bypassN <= 5) Plugin.L.LogInfo($"weapon requirement skipped (#{bypassN}): {key}");
            __result = true;
            return false;
        }

        static void DurabilityPostfix(Il2CppObjectBase __instance, ref bool __result)
        {
            if (!__result) return;
            try { if (NoHand(__instance.Pointer)) __result = false; }
            catch (Exception e) { if (++errN <= 5) Plugin.L.LogError("durability check: " + e); }
        }
        /* true = the yield had no target (an empty slot) and the iterator is now finished, as after its single yield */
        [MethodImpl(MethodImplOptions.NoInlining)]
        static bool NoHand(IntPtr it)
        {
            IntPtr cur = Plugin.IterGetRef(it, Iter, "<>2__current");
            var st = cur == IntPtr.Zero ? null : new Il2CppSystem.Object(cur).TryCast<StatusExecutionData>();
            if (st == null || st._Target_k__BackingField != null) return false;
            if (!Plugin.IterClearRef(it, Iter, "<>2__current") || !Plugin.IterSetInt(it, Iter, "<>1__state", -1)) return false;
            if (++noHandN <= 5) Plugin.L.LogInfo($"durability cost with an empty slot skipped (#{noHandN})");
            return true;
        }
    }
}
