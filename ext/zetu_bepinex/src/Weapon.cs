// skills_without_weapon (zetu.c parts 1 and 1b).
// The game checks a skill's RequiredEquipment ("Equipment_Blunt", ...) in ONE place for use (player and AI):
// Refactor.Combat.EquipmentCondition.CheckCondition looks through the equipment slots, and when none matches asks
// Refactor.SkillReader.HasEquipBypass(key) (the game's own bypass: Fire Battle Mage, Nature Spirit, Brick). Here
// HasEquipBypass answers yes for every "Equipment_*" key. Learning is never blocked by the weapon in the stock game; its
// "you need X, continue anyway?" text and the tooltip's red line ask the same bypass. Prerequisites, levels, skill points,
// ammo (arrows) and the weapon passives (Healing Touch, Two-Hand Grip, Shield Handling) stay stock.
// 1b. A cast with an EMPTY main hand: DurabilityCondition.<OnSuccessCondition>d__2.MoveNext yields
// Execution.AddStatus(<item in the slot>, Durability, -cost) with Target null, and the game's FX throws a
// NullReferenceException at every such cast. That one yield is dropped (nothing to wear); with an item in the slot
// nothing changes. Depends only on game state (same result on every peer).
using System;
using HarmonyLib;
using Refactor;
using Refactor.Combat;

namespace ZetuSuite
{
    static class Weapon
    {
        static int bypassN, noHandN;

        internal static void Install()
        {
            var h = new Harmony(Plugin.Guid + ".weapon");
            bool ok = Plugin.Patch(h, typeof(Weapon), typeof(SkillReader), nameof(SkillReader.HasEquipBypass), 1, prefix: nameof(BypassPrefix))
                   && Plugin.Patch(h, typeof(Weapon), typeof(DurabilityCondition._OnSuccessCondition_d__2), "MoveNext", 0, postfix: nameof(DurabilityPostfix));
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

        static void DurabilityPostfix(DurabilityCondition._OnSuccessCondition_d__2 __instance, ref bool __result)
        {
            if (!__result) return;
            var cur = __instance.__2__current;
            var st = cur?.TryCast<StatusExecutionData>();
            if (st == null || st._Target_k__BackingField != null) return;
            __instance.__2__current = null;
            __instance.__1__state = -1;                                 // finished, as after its single yield
            __result = false;
            if (++noHandN <= 5) Plugin.L.LogInfo($"durability cost with an empty slot skipped (#{noHandN})");
        }
    }
}
