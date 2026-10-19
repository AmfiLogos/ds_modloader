// ZET-U Mod Suite (zetu-suite) as a BepInEx 6 IL2CPP plugin: the same two gameplay settings as the mod loader's code
// module (loader/modules/zetu-suite: zetu.c, zetu_ui.c), on the same game methods, by Harmony patches. Build 25846389
// (DS_B.0.4.29).
//   skills_without_weapon (default true): skills of a skill tree work without that tree's weapon (Weapon.cs).
//   max_units (4..8, default 4): units on one expedition, and the screens that show them (Party.cs, Screens.cs).
// Settings: BepInEx/config/amfi.zetu-suite.cfg, read at game start. A save loaded with the plugin on asks first and keeps
// a dated copy of the world's save files (SaveWarning.cs). When the mod loader's own zetu-suite module is in the process,
// this plugin does nothing (one log line): the loader's module has the same patches.
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using BepInEx;
using BepInEx.Configuration;
using BepInEx.Logging;
using BepInEx.Unity.IL2CPP;
using HarmonyLib;

namespace ZetuSuite
{
    [BepInPlugin(Guid, "ZET-U Mod Suite", Version)]
    public class Plugin : BasePlugin
    {
        public const string Guid = "amfi.zetu-suite";
        public const string Version = "0.1.0";
        internal static ManualLogSource L;
        internal static bool SkillsWithoutWeapon;
        internal static int MaxUnits;

        public override void Load()
        {
            L = Log;
            var skills = Config.Bind("Gameplay", "skills_without_weapon", true,
                "Skills without weapon requirement: a unit can use the skills of a skill tree without holding that tree's weapon. " +
                "Prerequisite skills, level requirements and skill point costs stay the same.");
            var units = Config.Bind("Gameplay", "max_units", 4, new ConfigDescription(
                "Max units in the dungeon: how many units can go on one expedition. 4 is the game's normal value.",
                new AcceptableValueRange<int>(4, 8)));
            SkillsWithoutWeapon = skills.Value;
            MaxUnits = Math.Clamp(units.Value, 4, 8);
            string loader = LoaderModule();
            if (loader != null)
            {
                L.LogWarning($"ZET-U Mod Suite {Version}: OFF, the mod loader's ZET-U Mod Suite module runs in this game ({loader}); its settings apply");
                return;
            }
            L.LogInfo($"ZET-U Mod Suite {Version}: skills without weapon requirement {(SkillsWithoutWeapon ? "on" : "off")}, " +
                      $"max units in the dungeon {MaxUnits} (config {Path.GetFileName(Config.ConfigFilePath)})");
            if (SkillsWithoutWeapon) Weapon.Install();
            if (MaxUnits != 4) Party.Install();               // 4 = the stock table value: nothing to change
            SaveWarning.Install(Path.Combine(Paths.BepInExRootPath, "ZetuSuite"));
        }

        // ---------------------------------------------------------------- the mod loader's module in this process
        // The loader opens its code modules before GameAssembly is mapped (loader/src/dsl_main.c dsl_core_start), so an
        // enabled zetu-suite module is loaded before any BepInEx plugin. Windows: zetu-suite.dll; Mac: zetu-suite.dylib.
        static string LoaderModule()
        {
            try
            {
                if (OperatingSystem.IsWindows()) return GetModuleHandleW("zetu-suite.dll") != IntPtr.Zero ? "zetu-suite.dll" : null;
                if (OperatingSystem.IsMacOS())
                {
                    uint n = _dyld_image_count();
                    for (uint i = 0; i < n; i++)
                    {
                        string p = Marshal.PtrToStringUTF8(_dyld_get_image_name(i));
                        if (p != null && p.EndsWith("/zetu-suite.dylib", StringComparison.Ordinal)) return "zetu-suite.dylib";
                    }
                }
            }
            catch (Exception e) { L.LogWarning("loader module check failed: " + e.Message); }
            return null;
        }
        [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);
        [DllImport("libSystem.dylib")] static extern uint _dyld_image_count();
        [DllImport("libSystem.dylib")] static extern IntPtr _dyld_get_image_name(uint i);

        // ---------------------------------------------------------------- one patch; false = not installed (logged).
        // The game method by class, name and parameter count (as the loader's hook_method). Each part has its own Harmony
        // id: a part with a missing method takes all of its patches out (UnpatchSelf).
        internal static bool Patch(Harmony h, Type owner, Type t, string name, int argc, string prefix = null, string postfix = null)
        {
            try
            {
                MethodInfo m = null; int found = 0;
                foreach (var c in AccessTools.GetDeclaredMethods(t))
                    if (c.Name == name && c.GetParameters().Length == argc) { m = c; found++; }
                if (found != 1) { L.LogError($"{t.FullName}.{name} ({argc} args): {found} methods found, not patched"); return false; }
                h.Patch(m, prefix == null ? null : new HarmonyMethod(AccessTools.Method(owner, prefix)),
                           postfix == null ? null : new HarmonyMethod(AccessTools.Method(owner, postfix)));
                return true;
            }
            catch (Exception e) { L.LogError($"patch {t.FullName}.{name} failed: {e}"); return false; }
        }
    }
}
