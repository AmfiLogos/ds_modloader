// max_units (zetu.c part 2): units on one expedition. The game's value is the static int
// Refactor.SystemVar.Campaign.<PlayerPartyMax>k__BackingField, set by Campaign.Init from GameSystemTable row
// GAMESYSTEM_Campaign_PlayerPartyMax (4). Every reader inlines the static (no getter call), so Campaign.Init gets a
// postfix that sets the static to the setting. Every reader then uses it: the expedition risk "fewer units than the
// maximum" included. The screens built with 4 panels / portraits grow to the setting in Screens.cs.
using HarmonyLib;
using Il2CppInterop.Runtime;
using Refactor;

namespace ZetuSuite
{
    static class Party
    {
        internal static int Stock = 4;                                 // the table value (the count text "/4" of Screens.cs)

        internal static void Install()
        {
            var h = new Harmony(Plugin.Guid + ".party");
            if (!Plugin.Patch(h, typeof(Party), typeof(SystemVar.Campaign), nameof(SystemVar.Campaign.Init), 0, postfix: nameof(InitPostfix)))
            {
                h.UnpatchSelf(); Plugin.L.LogError("max units in the dungeon: OFF (SystemVar.Campaign.Init not found)"); return;
            }
            // Campaign.Init may have run already (tables loaded)
            var k = IL2CPP.GetIl2CppNestedType(IL2CPP.GetIl2CppClass("Assembly-CSharp.dll", "Refactor", "SystemVar"), "Campaign");
            if (k != System.IntPtr.Zero) IL2CPP.il2cpp_runtime_class_init(k);
            int v = SystemVar.Campaign._PlayerPartyMax_k__BackingField;
            if (v > 0) Set(v);
            Plugin.L.LogInfo($"units in the dungeon: {Plugin.MaxUnits} (game value now {v})");
            if (!Screens.Install(h))
            {
                h.UnpatchSelf();
                if (v > 0) SystemVar.Campaign._PlayerPartyMax_k__BackingField = Stock;
                Plugin.L.LogError("max units in the dungeon: OFF (the expedition screens could not be patched)");
            }
        }

        static void Set(int v)
        {
            if (v > 0 && v != Plugin.MaxUnits) Stock = v;
            SystemVar.Campaign._PlayerPartyMax_k__BackingField = Plugin.MaxUnits;
        }

        static void InitPostfix()
        {
            int v = SystemVar.Campaign._PlayerPartyMax_k__BackingField;
            Set(v);
            Plugin.L.LogInfo($"Campaign.Init: PlayerPartyMax {v} (table) -> {Plugin.MaxUnits}");
        }
    }
}
