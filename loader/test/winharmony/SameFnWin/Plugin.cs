using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using BepInEx;
using BepInEx.Logging;
using BepInEx.Unity.IL2CPP;
using HarmonyLib;
using Il2CppInterop.Common;

namespace SameFnWin;

/// TEST ONLY (Windows): the Mac plugin loader/test/bie/SameFn (Harmony postfix on Refactor.Main.MainFlow.Update, the
/// function the multiplayer module hooks for its command pump through the loader's MinHook engine) plus a record of the
/// function's first bytes before and after Harmony patches it: which engine patched first, where the loader's MinHook
/// trampoline returns, and how many bytes Harmony's native detour wrote. Every line also goes to
/// BepInEx\samefn_win.log, written and closed per line (a crash loses BepInEx's own buffered log lines).
/// Same log lines as the Mac plugin ("SAMEFN harmony patched MainFlow.Update", "SAMEFN harmony postfix #n").
[BepInPlugin("dsml.samefn", "SameFn", "0.2.0")]
public class Plugin : BasePlugin
{
    static ManualLogSource L; static long Calls; static string LogPath; static long Thunk;

    [DllImport("kernel32", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);

    static void W(string s)
    {
        try { File.AppendAllText(LogPath, DateTime.Now.ToString("HH:mm:ss.fff") + " " + s + Environment.NewLine); } catch { }
        L.LogInfo(s);
    }

    static byte[] Read(long a, int n) { var b = new byte[n]; for (int i = 0; i < n; i++) b[i] = Marshal.ReadByte(new IntPtr(a + i)); return b; }
    static string Hex(byte[] b) => string.Join(" ", b.Select(x => x.ToString("x2")));

    /// the native code of a generated il2cpp method: MethodInfo->methodPointer (first field of Il2CppMethodInfo)
    static long NativeCode(MethodInfo m)
    {
        FieldInfo f = null;
        try { f = Il2CppInteropUtils.GetIl2CppMethodInfoPointerFieldForGeneratedMethod(m); } catch (Exception e) { W("SAMEFN method info field: " + e.Message); }
        f ??= m.DeclaringType.GetFields(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic)
                 .FirstOrDefault(x => x.Name.StartsWith("NativeMethodInfoPtr_Update_"));
        if (f == null) return 0;
        var mi = (IntPtr)f.GetValue(null);
        return mi == IntPtr.Zero ? 0 : Marshal.ReadIntPtr(mi).ToInt64();
    }

    /// what is at the site before Harmony: stock code, or the loader's hook
    /// (site: E9 rel32 -> MinHook relay "FF 25 00000000 <abs>" -> dsload thunk "48 8B 05 11 00 00 00 ...",
    ///  thunk+24 = replacement, thunk+32 = MinHook trampoline = copied first instructions + "FF 25 00000000 <site+n>")
    static int Describe(long site)
    {
        var b = Read(site, 16);
        if (b[0] != 0xE9) { W("SAMEFN first: harmony (the site was not hooked yet: " + Hex(b) + ")"); return 0; }
        long relay = site + 5 + BitConverter.ToInt32(b, 1);
        var r = Read(relay, 14);
        long thunk = r[0] == 0xFF && r[1] == 0x25 && BitConverter.ToInt32(r, 2) == 0 ? BitConverter.ToInt64(r, 6) : relay;
        var t = Read(thunk, 40);
        bool ours = t.Take(7).SequenceEqual(new byte[] { 0x48, 0x8B, 0x05, 0x11, 0x00, 0x00, 0x00 });
        if (!ours) { W($"SAMEFN first: another hook (jump to {thunk:x}, not a dsload thunk: {Hex(t.Take(16).ToArray())})"); return 0; }
        Thunk = thunk; long tramp = BitConverter.ToInt64(t, 32);
        var tr = Read(tramp, 80); int ret = -1, copied = -1;
        for (int i = 0; i + 14 <= tr.Length; i++)
            if (tr[i] == 0xFF && tr[i + 1] == 0x25 && BitConverter.ToInt32(tr, i + 2) == 0)
            {
                long to = BitConverter.ToInt64(tr, i + 6);
                if (to > site && to < site + 32) { ret = (int)(to - site); copied = i; break; }
            }
        W($"SAMEFN first: dsload (MinHook jump at the site), relay {relay:x}, dsload thunk {thunk:x}, replacement {BitConverter.ToInt64(t, 24):x}, " +
          $"trampoline {tramp:x} ({copied} bytes of copied code) returns to site+{ret}");
        return ret;
    }

    public override void Load()
    {
        L = Log; LogPath = Path.Combine(Paths.BepInExRootPath, "samefn_win.log");
        W($"SAMEFN load (pid {Environment.ProcessId}, DOORSTOP_DISABLE in the process: '{Environment.GetEnvironmentVariable("DOORSTOP_DISABLE")}')");
        var t = AccessTools.TypeByName("Refactor.Main.MainFlow");
        var m = t == null ? null : AccessTools.Method(t, "Update");
        if (m == null) { W("SAMEFN harmony MISS"); return; }
        long ga = GetModuleHandleW("GameAssembly.dll").ToInt64();
        long site = NativeCode(m);
        int ret = 0; byte[] before = null;
        if (site != 0)
        {
            before = Read(site, 32);
            W($"SAMEFN site {site:x} rva {(ga != 0 ? site - ga : 0):x} before: {Hex(before)}");
            try { ret = Describe(site); } catch (Exception e) { W("SAMEFN describe failed: " + e.Message); }
        }
        else W("SAMEFN site unknown (no MethodInfo pointer)");
        try { new Harmony("dsml.samefn").Patch(m, postfix: new HarmonyMethod(typeof(Plugin), nameof(Post))); }
        catch (Exception e) { W("SAMEFN harmony patch FAILED: " + e); return; }
        W("SAMEFN harmony patched MainFlow.Update");
        if (site == 0) return;
        long site2 = NativeCode(m);
        var after = Read(site, 32);
        int changed = 0, reach = 0;
        for (int i = 0; i < 32; i++) if (after[i] != before[i]) { changed++; reach = i + 1; }
        W($"SAMEFN site after harmony: {Hex(after)}; {changed} byte(s) changed, up to site+{reach}; method pointer {(site2 == site ? "unchanged" : $"now {site2:x}")}");
        if (ret > 0)
            W($"SAMEFN overlap: harmony wrote up to site+{reach}, the dsload trampoline returns to site+{ret}: " +
              (ret < reach ? "INSIDE harmony's patch (the original code reached through dsload would run torn bytes)" : "after harmony's patch (sound)"));
        // BepInEx follows E9 / FF 25 jumps before Dobby patches (INativeDetour.FollowExportThunks): with our hook first it
        // lands on the dsload thunk, so Dobby's jump is there, not at the site
        if (Thunk != 0)
        {
            var th = Read(Thunk, 8);
            W($"SAMEFN harmony detour on: {(th[0] == 0xFF && th[1] == 0x25 ? "the dsload thunk" : "not the dsload thunk")} {Thunk:x} " +
              $"({Hex(th)}; {Math.Abs(Thunk - ga) / (1 << 20)} MB from GameAssembly)");
        }
    }

    static void Post() { if (++Calls % 600 == 1) W($"SAMEFN harmony postfix #{Calls}"); }
}
