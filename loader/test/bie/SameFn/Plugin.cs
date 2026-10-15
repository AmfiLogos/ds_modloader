using BepInEx;
using BepInEx.Logging;
using BepInEx.Unity.IL2CPP;
using HarmonyLib;

namespace SameFn;

/// TEST ONLY: Harmony postfix on Refactor.Main.MainFlow.Update, the function the multiplayer module (a dsload module)
/// hooks for its command pump. The test reads this count and the multiplayer frame counter.
[BepInPlugin("dsml.samefn", "SameFn", "0.1.0")]
public class Plugin : BasePlugin
{
    static ManualLogSource L; static long Calls;
    public override void Load()
    {
        L = Log;
        var t = AccessTools.TypeByName("Refactor.Main.MainFlow");
        var m = t == null ? null : AccessTools.Method(t, "Update");
        if (m == null) { L.LogWarning("SAMEFN harmony MISS"); return; }
        new Harmony("dsml.samefn").Patch(m, postfix: new HarmonyMethod(typeof(Plugin), nameof(Post)));
        L.LogInfo("SAMEFN harmony patched MainFlow.Update");
    }
    static void Post() { if (++Calls % 600 == 1) L.LogInfo($"SAMEFN harmony postfix #{Calls}"); }
}
