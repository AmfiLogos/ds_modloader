// ZET-U Mod Suite (zetu-suite) as a BepInEx 6 IL2CPP plugin: the same two gameplay settings as the mod loader's code
// module (loader/modules/zetu-suite: zetu.c, zetu_ui.c), on the same game methods, by Harmony patches. Build 25846389
// (DS_B.0.4.29).
//   skills_without_weapon (default true): skills of a skill tree work without that tree's weapon (Weapon.cs).
//   max_units (4..8, default 4): units on one expedition, and the screens that show them (Party.cs, Screens.cs).
// Settings: BepInEx/config/amfi.zetu-suite.cfg, read at game start. 0.1.2: no save warning / backup any more (tests: saves
// made with the plugin load and play in the normal game; the old code is in old/SaveWarning.cs, not built). When the mod
// loader's own zetu-suite module is in the process, this plugin does nothing (one log line): the loader's module has the
// same patches.
// BepInEx's console window is turned off at every start (ConsoleOff); its log file BepInEx/LogOutput.log stays on.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using BepInEx;
using BepInEx.Configuration;
using BepInEx.Logging;
using BepInEx.Unity.IL2CPP;
using HarmonyLib;
using Il2CppInterop.Runtime;

namespace ZetuSuite
{
    [BepInPlugin(Guid, "ZET-U Mod Suite", Version)]
    public class Plugin : BasePlugin
    {
        public const string Guid = "amfi.zetu-suite";
        public const string Version = "0.1.3";
        internal static ManualLogSource L;
        internal static bool SkillsWithoutWeapon;
        internal static int MaxUnits;

        public override void Load()
        {
            L = Log;
            ConsoleOff();
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
        }

        // ---------------------------------------------------------------- no BepInEx console window (owner rule)
        // BepInEx's own setting [Logging.Console] Enabled = false, saved in BepInEx/config/BepInEx.cfg: the next starts open
        // no console. The console this start opened (BepInEx opens it before any plugin) is closed now: its log listener
        // out, and on Windows the window freed (BepInEx cannot detach a Unix console: there the listener only, so no
        // more console output). Disk logging ([Logging.Disk], BepInEx/LogOutput.log) stays as it is.
        static void ConsoleOff()
        {
            try
            {
                bool was = ConsoleManager.ConfigConsoleEnabled.Value;
                if (was) { ConsoleManager.ConfigConsoleEnabled.Value = false; ConfigFile.CoreConfig.Save(); }
                var cl = Logger.Listeners.OfType<ConsoleLogListener>().ToList();
                foreach (var c in cl) { Logger.Listeners.Remove(c); c.Dispose(); }
                bool win = OperatingSystem.IsWindows() && ConsoleManager.ConsoleActive;
                if (win) ConsoleManager.DetachConsole();
                L.LogInfo(was || cl.Count > 0 || win
                    ? $"BepInEx console: {(cl.Count > 0 || win ? "closed" : "not open")}, off in {Path.GetFileName(ConfigFile.CoreConfig.ConfigFilePath)} for the next starts; the log file stays on"
                    : $"BepInEx console: off ({Path.GetFileName(ConfigFile.CoreConfig.ConfigFilePath)}); the log file stays on");
            }
            catch (Exception e) { L.LogWarning("BepInEx console: could not turn it off: " + e.Message); }
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

        // ---------------------------------------------------------------- compiler-made iterators: by METHOD name, never by number
        // The C# compiler names the iterator of method M "<M>d__N" (interop type "_M_d__N"); N changes with almost every
        // game build (CampaignLoadingEventHandler.LoadCampaign: d__67, d__70, d__72, d__73). A method that names such a type
        // fails to JIT on any other build (TypeLoadException) before its own try/catch runs: 0.1.1 stopped every world load
        // on other builds (player report 2026-10-26). So no iterator type is named in this DLL (build.sh refuses one):
        // patch time finds the interop type by its name prefix, run time checks the il2cpp class name and reaches the
        // compiler's fields ("<>1__state", "<>2__current") through the il2cpp API.
        internal static Type IterType(Type owner, string method)
        {
            string pre = "_" + method + "_d__"; Type t = null; int n = 0;
            foreach (var c in owner.GetNestedTypes(BindingFlags.Public | BindingFlags.NonPublic))
                if (c.Name.StartsWith(pre, StringComparison.Ordinal)) { t = c; n++; }
            if (n != 1) { L.LogError($"{owner.FullName}: {n} iterators of {method} found, not patched"); return null; }
            return t;
        }
        /* the il2cpp field of an iterator object whose class is "<method>d__N"; IntPtr.Zero when it is not (one error line
           per method and field: the caller then leaves the iterator as the game runs it) */
        internal static IntPtr IterField(IntPtr obj, string method, string field)
        {
            IntPtr k = obj == IntPtr.Zero ? IntPtr.Zero : IL2CPP.il2cpp_object_get_class(obj);
            string name = k == IntPtr.Zero ? null : Marshal.PtrToStringUTF8(IL2CPP.il2cpp_class_get_name(k));
            IntPtr f = name != null && name.StartsWith("<" + method + ">d__", StringComparison.Ordinal) ? IL2CPP.il2cpp_class_get_field_from_name(k, field) : IntPtr.Zero;
            if (f == IntPtr.Zero) lock (iterMissed) if (iterMissed.Add(method + " " + field))
                L.LogError($"iterator of {method}: no field {field} (class {name ?? "none"}): left as the game runs it");
            return f;
        }
        static readonly HashSet<string> iterMissed = new HashSet<string>();
        internal static unsafe bool IterSetInt(IntPtr obj, string method, string field, int v)
        {
            IntPtr f = IterField(obj, method, field);
            if (f == IntPtr.Zero) return false;
            IL2CPP.il2cpp_field_set_value(obj, f, &v);                 // a value type: a pointer to the value
            return true;
        }
        internal static unsafe IntPtr IterGetRef(IntPtr obj, string method, string field)
        {
            IntPtr f = IterField(obj, method, field), v = IntPtr.Zero;
            if (f != IntPtr.Zero) IL2CPP.il2cpp_field_get_value(obj, f, &v);   // the object pointer is copied into v
            return v;
        }
        internal static bool IterClearRef(IntPtr obj, string method, string field)
        {
            IntPtr f = IterField(obj, method, field);
            if (f == IntPtr.Zero) return false;
            unsafe { IL2CPP.il2cpp_field_set_value(obj, f, null); }    // a reference: the object pointer itself (null here)
            return true;
        }

        // ---------------------------------------------------------------- one patch; false = not installed (logged).
        // The game method by class, name and parameter count (as the loader's hook_method). Each part has its own Harmony
        // id: a part with a missing method takes all of its patches out (UnpatchSelf).
        /* orArgc: the parameter count of the same method on other game builds (-1: none). Example: DungeonResultView.SetPlayerData
           has 3 parameters up to build 25284551 and 4 on 25846389; 0.1.2 asked for 4 only, so on a player's older build the
           whole max-units part was OFF. */
        internal static bool Patch(Harmony h, Type owner, Type t, string name, int argc, string prefix = null, string postfix = null,
                                   int orArgc = -1)
        {
            try
            {
                MethodInfo m = null; int found = 0;
                foreach (var c in AccessTools.GetDeclaredMethods(t))
                {
                    int n = c.GetParameters().Length;
                    if (c.Name == name && (n == argc || n == orArgc)) { m = c; found++; }
                }
                string args = orArgc < 0 ? $"{argc}" : $"{argc} or {orArgc}";
                if (found != 1) { L.LogError($"{t.FullName}.{name} ({args} args): {found} methods found, not patched"); return false; }
                h.Patch(m, prefix == null ? null : new HarmonyMethod(AccessTools.Method(owner, prefix)),
                           postfix == null ? null : new HarmonyMethod(AccessTools.Method(owner, postfix)));
                return true;
            }
            catch (Exception e) { L.LogError($"patch {t.FullName}.{name} failed: {e}"); return false; }
        }
    }
}
