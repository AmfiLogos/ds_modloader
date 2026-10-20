// The save warning and backup (as the mod loader's mod.json "save_dependent" + "save_backup", loader/src/dsl_saves.c).
// Loading a save while this plugin runs asks first, with the game's notice box: Load / Cancel. Asked once per world
// (kept in BepInEx/ZetuSuite/warned_worlds.txt). Load first makes byte copies of the world's save files (the loaded file +
// every other save of the same world guid) in BepInEx/ZetuSuite/save_backups/<YYYY-MM-DD_HHMM>_<clan>/: copies, never a
// move, never inside the game's Saves folder (the game lists it recursively, and it syncs to Steam Cloud). A copy that
// fails = the world is NOT loaded, a notice says why, and the question comes again next time.
// Hooked at EventQueue.AddEvent(LoadCampaignRequested) (no loading screen yet). A load that does not come through the
// queue is stopped in CampaignLoadingEventHandler.LoadCampaign (its iterator ends, the game's own cancel notice closes
// the loading screen) and asked the same way a few frames later.
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using BepInEx;
using BepInEx.Unity.IL2CPP;
using HarmonyLib;
using Il2CppInterop.Runtime;
using Refactor;
using Refactor.Main;
using Refactor.Main.Event;
using Refactor.UI;
using UnityEngine;
using UnityEngine.UI;
using UObject = UnityEngine.Object;

namespace ZetuSuite
{
    static class SaveWarning
    {
        static string dir, backups, warnedFile;
        static readonly HashSet<string> warned = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        static EventQueue queue;
        static bool pass, open;
        static string wName, wGuid, wBackup;
        static int askIn = -1, failIn = -1;                             // frames until the question / the failure notice
        static string failText;
        static int answer;                                              // 1 Load, 2 Cancel (run in the next frame)
        internal static int Warned, BackedUp;                           // tests / log

        internal static void Install(string root)
        {
            dir = root; backups = Path.Combine(root, "save_backups"); warnedFile = Path.Combine(root, "warned_worlds.txt");
            try { if (File.Exists(warnedFile)) foreach (var l in File.ReadAllLines(warnedFile)) if (l.Trim().Length > 0) warned.Add(l.Trim()); }
            catch (Exception e) { Plugin.L.LogWarning($"{warnedFile}: {e.Message}"); }
            var h = new Harmony(Plugin.Guid + ".saves");
            bool queueHook = Plugin.Patch(h, typeof(SaveWarning), typeof(EventQueue), nameof(EventQueue.AddEvent), 1, prefix: nameof(AddEventPrefix));
            bool loadHook = Plugin.Patch(h, typeof(SaveWarning), typeof(CampaignLoadingEventHandler), nameof(CampaignLoadingEventHandler.LoadCampaign), 1, postfix: nameof(LoadCampaignPostfix));
            if (!queueHook && !loadHook) { h.UnpatchSelf(); Plugin.L.LogError("save warning: OFF (no load hook)"); return; }
            IL2CPPChainloader.AddUnityComponent(typeof(Ticker));
            Plugin.L.LogInfo($"save warning: on ({warned.Count} world(s) already answered); backups in {backups}");
        }

        // ------------------------------------------------------------------ which loads ask first
        static bool NeedsWarning(string name, out string guid)
        {
            guid = null;
            if (string.IsNullOrEmpty(name)) return false;
            string p = SavePath(name);
            if (p == null || !File.Exists(p)) return false;             // not a save file (the game handles it)
            guid = NameGuid(name) ?? Header(p, out _) ?? "save:" + name;
            return !warned.Contains(guid);
        }
        static string SavePath(string name)
        {
            try { return SaveLoadHelper.GetCampaignSavePath(name); }
            catch (Exception e) { Plugin.L.LogWarning("GetCampaignSavePath: " + e.Message); return null; }
        }
        /* "__AutoSave_<32 hex>_": the world's guid is in the name (lower case, with dashes) */
        static string NameGuid(string f)
        {
            int a = f.IndexOf("__AutoSave_", StringComparison.Ordinal);
            if (a < 0 || f.Length < a + 11 + 32) return null;
            string h = f.Substring(a + 11, 32);
            foreach (char c in h) if (!Uri.IsHexDigit(c)) return null;
            h = h.ToLowerInvariant();
            return $"{h.Substring(0, 8)}-{h.Substring(8, 4)}-{h.Substring(12, 4)}-{h.Substring(16, 4)}-{h.Substring(20, 12)}";
        }
        /* the save's "CampaignSaveHeader" block: CampaignGuid (lower case) and ClanName; read in 256 KB steps */
        static string Header(string path, out string clan)
        {
            clan = "";
            try
            {
                using var fs = File.OpenRead(path);
                var buf = new byte[256 << 10]; var sb = new StringBuilder(); int r;
                while ((r = fs.Read(buf, 0, buf.Length)) > 0)
                {
                    sb.Append(Encoding.UTF8.GetString(buf, 0, r));
                    string s = sb.ToString();
                    int h = s.IndexOf("\"CampaignSaveHeader\"", StringComparison.Ordinal);
                    if (h >= 0 && (s.IndexOf("\"SaveTime\"", h, StringComparison.Ordinal) >= 0 || fs.Position >= fs.Length))
                    {
                        string blk = s.Substring(h, Math.Min(4096, s.Length - h));
                        clan = Str(blk, "ClanName");
                        string g = Str(blk, "CampaignGuid").ToLowerInvariant();
                        return g.Length == 36 ? g : null;
                    }
                    if (h < 0 && sb.Length > 8192) sb.Remove(0, sb.Length - 8192);     // the key may span two reads
                }
            }
            catch (Exception e) { Plugin.L.LogWarning($"{path}: {e.Message}"); }
            return null;
        }
        static string Str(string h, string key)
        {
            int p = h.IndexOf($"\"{key}\"", StringComparison.Ordinal); if (p < 0) return "";
            p = h.IndexOf(':', p + key.Length + 2); if (p < 0) return "";
            p = h.IndexOf('"', p); if (p < 0) return "";
            int e = h.IndexOf('"', p + 1); return e < 0 ? "" : h.Substring(p + 1, e - p - 1);
        }

        // ------------------------------------------------------------------ the hooks
        static bool AddEventPrefix(EventQueue __instance, IEventData gameEventData)
        {
            if (pass || gameEventData == null) return true;
            try
            {
                var ev = gameEventData.TryCast<LoadCampaignRequested>();
                if (ev == null) return true;
                string name = ev.CampaignName;
                if (!NeedsWarning(name, out string g)) return true;
                queue = __instance;
                Hold(name, g, 0);
                return false;                                           // the stock load never starts: no loading screen
            }
            catch (Exception e) { Plugin.L.LogError("save warning: " + e); return true; }
        }
        static void LoadCampaignPostfix(CampaignLoadingEventHandler __instance, string campaignName, Il2CppSystem.Collections.IEnumerator __result)
        {
            if (pass) return;
            try
            {
                if (!NeedsWarning(campaignName, out string g)) return;
                var it = __result?.TryCast<CampaignLoadingEventHandler._LoadCampaign_d__73>();
                if (it == null) { Plugin.L.LogError($"save warning: load of {campaignName} not held (no iterator)"); return; }
                it.__1__state = -2;                                     // the load ends before its first step
                __instance.CancelIronModeLoad("This world is one of your game saves.", "zetu-suite: save warning first");
                Plugin.L.LogInfo($"save warning: load of {campaignName} held (not through the event queue)");
                Hold(campaignName, g, 3);                               // after the stock box that closes the load
            }
            catch (Exception e) { Plugin.L.LogError("save warning: " + e); }
        }
        static void Hold(string name, string guid, int frames)
        {
            if (open) { Plugin.L.LogInfo($"save warning: load of {name} held: the question for {wName} is still open"); return; }
            wName = name; wGuid = guid; open = true; askIn = frames;
        }

        // ------------------------------------------------------------------ the question (the game's notice box)
        static CommonNoticeView View()
        {
            var o = UObject.FindObjectOfType(Il2CppType.Of<CommonNoticeView>(), true);
            return o != null ? o.TryCast<CommonNoticeView>() : null;
        }
        static float fs0, fsMin, fsMax; static bool autoWas, fitted;
        static bool Notice(string text, string ok, string cancel)
        {
            var v = View();
            if (v == null) { Plugin.L.LogError($"save warning: no notice box in this scene: \"{text}\" not shown"); return false; }
            Il2CppSystem.Action a = (Action)(() => answer = 1), c = (Action)(() => answer = 2);
            if (cancel != null) v.Show_Select(text, a, c, false); else v.Show_Confirm(text, a);
            var t = v._noticeText;
            if (t != null)
            {
                t.text = text;
                if (!fitted) { autoWas = t.enableAutoSizing; fs0 = t.fontSize; fsMin = t.fontSizeMin; fsMax = t.fontSizeMax; fitted = true; }
                t.fontSizeMax = t.fontSize; t.fontSizeMin = t.fontSize * 0.6f; t.enableAutoSizing = true;   // a long folder fits the box
            }
            if (v._txtConfirm != null) v._txtConfirm.text = ok;
            if (cancel != null && v._txtCancle != null) v._txtCancle.text = cancel;
            v.gameObject.SetActive(true);
            Raise(v, 32040);
            Plugin.L.LogInfo($"save warning: notice shown ({(cancel != null ? "two buttons" : "one button")}): {text.Replace('\n', ' ')}");
            return true;
        }
        static void Unfit()
        {
            var t = fitted ? View()?._noticeText : null;
            if (t == null) return;
            t.enableAutoSizing = autoWas; t.fontSizeMin = fsMin; t.fontSizeMax = fsMax; t.fontSize = fs0;     // the game's other notices
            fitted = false;
        }
        /* the notice above the Load window: its own canvas, sorting order `order` (as the loader's raise_view) */
        static void Raise(Component view, int order)
        {
            var cv = view.GetComponentInParent<Canvas>();
            if (cv == null) return;
            if (cv.rootCanvas == cv)
            {
                var g = view.gameObject;
                cv = g.GetComponent<Canvas>() ?? g.AddComponent<Canvas>();
                if (g.GetComponent<GraphicRaycaster>() == null) g.AddComponent<GraphicRaycaster>();
            }
            cv.overrideSorting = true; cv.sortingOrder = order;
        }
        /* the folder as shown: relative to the game folder when inside it; a zero-width space after each separator, so
           the box wraps the long path at a folder name, never in the middle of a word */
        static string Shown(string p)
        {
            string root = Paths.GameRootPath.TrimEnd('/', '\\') + Path.DirectorySeparatorChar;
            if (p.StartsWith(root, StringComparison.OrdinalIgnoreCase)) p = p.Substring(root.Length);
            return p.Replace("/", "/​").Replace("\\", "\\​");
        }
        static void Ask()
        {
            wBackup = BackupDirFor(wName);
            string text = "This world is one of your game saves. This mod changes what it saves: ZET-U Mod Suite. " +
                          "It may break later in the game without this mod.\n\nLoad first keeps a copy of its save files in:\n" + Shown(wBackup);
            Plugin.L.LogInfo($"save warning: shown for {wName} (world {wGuid}), backup to {wBackup}");
            if (!Notice(text, "Load", "Cancel")) { open = false; Plugin.L.LogError($"save warning: {wName} not loaded"); }
        }
        static void OnLoad()
        {
            open = false; Unfit();
            if (!BackupWorld(wName, wGuid, ref wBackup, out int nf, out string why))
            {
                Plugin.L.LogError($"save warning: backup of {wName} FAILED ({nf} file(s) copied to {wBackup}): {why}: NOT loaded");
                failText = "This world was not loaded: its save files could not be copied first.\n\n" + why;
                failIn = 3;                                             // after the question's box closed
                return;                                                 // not loaded, not marked as answered: asked again
            }
            BackedUp++;
            Plugin.L.LogInfo($"save warning: backup of {wName} (world {wGuid}): {nf} file(s) copied to {wBackup}");
            warned.Add(wGuid); Warned++;
            try { Directory.CreateDirectory(dir); File.AppendAllText(warnedFile, wGuid + Environment.NewLine); }
            catch (Exception e) { Plugin.L.LogWarning($"{warnedFile}: {e.Message}"); }
            Plugin.L.LogInfo($"save warning: answered Load for {wName} (world {wGuid})");
            Requeue();
        }
        static void Requeue()
        {
            if (queue == null) queue = UObject.FindObjectOfType<MainFlow>()?._eventQueue;
            if (queue == null) { Plugin.L.LogError($"save warning: cannot load {wName} again (no event queue)"); return; }
            var e = new LoadCampaignRequested(wName);
            pass = true;
            try { queue.AddEvent(e.Cast<IEventData>()); }
            finally { pass = false; }
        }

        // ------------------------------------------------------------------ the backup
        /* <backups>/<YYYY-MM-DD_HHMM>_<clan name of the save's header, else the save name>, file-safe on Mac and Windows */
        static string BackupDirFor(string name)
        {
            string p = SavePath(name), clan = "";
            if (p != null) Header(p, out clan);
            var sb = new StringBuilder();
            foreach (char c in string.IsNullOrEmpty(clan) ? name : clan)
            {
                bool ok = char.IsLetterOrDigit(c) || c == '-';
                if (!ok && (sb.Length == 0 || sb[sb.Length - 1] == '_')) continue;   // no leading or double '_'
                sb.Append(ok ? c : '_');
                if (sb.Length >= 48) break;
            }
            string safe = sb.ToString().TrimEnd('_');
            if (safe.Length == 0) safe = "world";
            return Path.Combine(backups, DateTime.Now.ToString("yyyy-MM-dd_HHmm") + "_" + safe);
        }
        static bool Inside(string path, string d)
        {
            string a = Path.GetFullPath(path).TrimEnd('/', '\\') + "/", b = Path.GetFullPath(d).TrimEnd('/', '\\') + "/";
            a = a.Replace('\\', '/'); b = b.Replace('\\', '/');
            return a.StartsWith(b, StringComparison.OrdinalIgnoreCase);
        }
        /* the world's save files (the loaded one first + every other one with its guid) copied into a new folder.
           Each file once: the game's path to the loaded save can be spelled differently from the folder listing
           (Windows: Unity's persistentDataPath has '/', Directory.GetFiles answers with '\\'), so files are compared by
           full path and by name (case-insensitive), never by the raw string. The folder is always a new one (never an
           older copy, nor a folder of the same minute); inside it a file is written over, so only a real copy failure
           stops the load. */
        static bool BackupWorld(string name, string guid, ref string bdir, out int nf, out string why)
        {
            nf = 0; why = "";
            try
            {
                string src = SavePath(name);
                if (src == null || !File.Exists(src)) { why = $"{name}.json is not there"; return false; }
                src = Path.GetFullPath(src);
                string saves = Path.GetDirectoryName(src);
                var files = new List<string> { src };
                var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { Path.GetFileName(src) };
                if (guid != null && !guid.StartsWith("save:", StringComparison.Ordinal))
                    foreach (var f0 in Directory.GetFiles(saves, "*.json"))
                    {
                        string f = Path.GetFullPath(f0);
                        if (names.Contains(Path.GetFileName(f))) continue;     // the loaded save itself, or a name already taken
                        string fn = Path.GetFileNameWithoutExtension(f);
                        string g = NameGuid(fn) ?? Header(f, out _);
                        if (string.Equals(g, guid, StringComparison.OrdinalIgnoreCase)) { files.Add(f); names.Add(Path.GetFileName(f)); }
                    }
                if (Inside(bdir, saves)) { why = $"{bdir} is inside the Saves folder"; return false; }
                string b0 = bdir;
                for (int k = 2; (Directory.Exists(bdir) || File.Exists(bdir)) && k < 100; k++) bdir = $"{b0}_{k}";   // never into an older copy
                if (Directory.Exists(bdir) || File.Exists(bdir)) bdir = $"{b0}_{DateTime.Now:ssfff}_{Guid.NewGuid().ToString("N").Substring(0, 6)}";
                Directory.CreateDirectory(bdir);
                foreach (var f in files)
                {
                    string dst = Path.Combine(bdir, Path.GetFileName(f));
                    File.Copy(f, dst, true);                            // our own new folder: written over, never refused
                    File.SetLastWriteTimeUtc(dst, File.GetLastWriteTimeUtc(f));
                    if (!SameBytes(f, dst)) { why = $"{dst}: the copy differs from the save file"; return false; }
                    nf++;
                }
                return true;
            }
            catch (Exception e) { why = e.Message; return false; }
        }
        static bool SameBytes(string a, string b)
        {
            var x = File.ReadAllBytes(a); var y = File.ReadAllBytes(b);
            return x.AsSpan().SequenceEqual(y);
        }

        // ------------------------------------------------------------------ one tick per frame (main thread)
        static void Tick()
        {
            if (askIn >= 0 && askIn-- == 0) Ask();
            if (answer != 0)
            {
                int a = answer; answer = 0;
                if (a == 1 && failText != null && !open) { failText = null; Unfit(); return; }   // the failure notice's OK
                if (a == 1) OnLoad();
                else { open = false; Unfit(); Plugin.L.LogInfo($"save warning: answered Cancel for {wName}: not loaded"); }
            }
            if (failIn >= 0 && failIn-- == 0) { if (!Notice(failText, "OK", null)) failText = null; }
        }
        public class Ticker : MonoBehaviour
        {
            public Ticker(IntPtr p) : base(p) { }
            void Update() { try { Tick(); } catch (Exception e) { Plugin.L.LogError("save warning: " + e); } }
        }
    }
}
