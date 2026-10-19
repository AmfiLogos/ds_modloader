// zetu-probe: TEST ONLY BepInEx plugin for test/zetu_bie_test.py (never shipped; the C# twin of loader/test/zetu_probe.c).
// Drives the game's own flows from a request file polled every 10 frames on the main thread: <DSRIG_DIR>/zetu_probe_req.txt
// (no DSRIG_DIR: <BepInEx>/zetu_probe_req.txt), one line:
//   recruit              the game's Recruit button: candidates refreshed for free, gold / population room made if short,
//                        then CandidateRecruitRequested(0, Base, MapSizeMax/2-(0,2), Player)
//   select_all           SelectAllUnitRequested;  toggle <n>  ToggleEntityUIRequested(n): 6 Status, 7 Skill, 8 Inventory
//   result               ShowDungeonResultRequested (the dungeon's "Return to base" button)
//   risks                the risk rows of the expedition window's supplies step
//   unequip <map> <guid> UnequipItemRequested(map, unit, slot 0 = the weapon hand, amount 1)
//   unequip_all <map>    the same for every live player unit on that map (0 Base, 1 Dungeon)
//   units                live player units per map (Base, Dungeon)
//   units_list <map>     their guids
//   approach <map>       every live player unit on that map walks to the nearest live hostile unit (a direct move order,
//                        as the rig's order_move), so a cast below is in range
//   cast <map> <unit guid> <skill key>   UseSkillTargetRequested(map, unit, nearest live hostile, skill) (the player's own
//                        skill order)
//   load <save name>     LoadCampaignRequested(name) into the game's event queue (the Load menu's way)
//   speed                the game speed: speed, requested, lock count, pause reasons
//   speed_unlock         GameSpeedLockRequested(Unlock) into the event queue (a window the test skipped keeps its lock)
//   speed_clear          RemovePauseReasonCount(PopupTimeLock) once (the same)
//   set_speed <n>        SetGameSpeedRequested(n): 0 Pause, 1 Normal, 2 Fast, 3 VeryFast
//   equip <map>          each live player unit on that map: the item key in equipment slot 0 (the weapon hand)
//   who <rva> ...        (Mac arm64 build 25846389) the Assembly-CSharp method that holds each GameAssembly address
//   scene                the active scene's name, and whether MainFlow is there
//   party                SystemVar.Campaign.PlayerPartyMax as the game reads it now
//   notice               the game's notice box: shown or not, its text and button labels
//   notice_ok | notice_cancel   OnClick_Confirm / OnClick_Cancle of the shown notice box
// ZETU_TEST_DATA (env, Windows VM tests): Application.persistentDataPath answers that folder, so the game's saves and
// settings stay in a test folder (never LocalLow, which syncs to Steam Cloud).
// Each answer is one BepInEx log line "zetu-probe: <cmd>: ...". Also counts every SkillReader.HasEquipBypass answer.
using System;
using System.IO;
using System.Runtime.InteropServices;
using BepInEx;
using BepInEx.Unity.IL2CPP;
using HarmonyLib;
using Il2CppInterop.Runtime;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
using Refactor;
using Refactor.Main;
using Refactor.Main.Event;
using Refactor.Map;
using Refactor.UI;
using UnityEngine;
using UObject = UnityEngine.Object;

namespace ZetuProbe
{
    [BepInPlugin("amfi.zetu-probe", "zetu probe (test)", "0.0.1")]
    public class Probe : BasePlugin
    {
        static string req;
        static BepInEx.Logging.ManualLogSource L;
        static int bypassTrue, bypassFalse;
        public override void Load()
        {
            L = Log;
            string d = Environment.GetEnvironmentVariable("DSRIG_DIR");
            req = Path.Combine(string.IsNullOrEmpty(d) ? Paths.BepInExRootPath : d, "zetu_probe_req.txt");
            AddComponent<Ticker>();
            var h = new Harmony("amfi.zetu-probe");
            h.Patch(AccessTools.Method(typeof(SkillReader), nameof(SkillReader.HasEquipBypass)), postfix: new HarmonyMethod(typeof(Probe), nameof(BypassPost)));
            data = Environment.GetEnvironmentVariable("ZETU_TEST_DATA");
            if (!string.IsNullOrEmpty(data))
            {
                h.Patch(AccessTools.PropertyGetter(typeof(Application), nameof(Application.persistentDataPath)), postfix: new HarmonyMethod(typeof(Probe), nameof(DataPost)));
                L.LogInfo($"zetu-probe: persistentDataPath -> {data} (now {Application.persistentDataPath})");
            }
            L.LogInfo($"zetu-probe: ready ({req})");
        }
        static string data;
        static void DataPost(ref string __result) { __result = data; }
        static void BypassPost(string key, bool __result) { if (__result) bypassTrue++; else bypassFalse++; }

        public class Ticker : MonoBehaviour
        {
            public Ticker(IntPtr p) : base(p) { }
            int frames;
            void Update()
            {
                if (++frames % 10 != 0 || !File.Exists(req)) return;
                string line;
                try { line = File.ReadAllText(req).Trim(); File.Move(req, req + ".done", true); } catch { return; }
                try { Run(line); } catch (Exception e) { L.LogError($"zetu-probe: {line}: {e}"); }
            }
        }

        static void Send(EventQueue q, Il2CppSystem.Object ev, string line) { q.AddEvent(ev.Cast<IEventData>()); L.LogInfo($"zetu-probe: {line}: sent"); }

        static void Run(string line)
        {
            var a = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            string cmd = a.Length > 0 ? a[0] : "";
            if (cmd == "party") { L.LogInfo($"zetu-probe: party: PlayerPartyMax {SystemVar.Campaign._PlayerPartyMax_k__BackingField}"); return; }
            if (cmd == "scene")
            {
                var sc = UnityEngine.SceneManagement.SceneManager.GetActiveScene();
                L.LogInfo($"zetu-probe: scene: {sc.name} mainflow {UObject.FindObjectOfType<MainFlow>() != null}");
                return;
            }
            if (cmd == "who")
            {
                IntPtr hb = Marshal.ReadIntPtr(IL2CPP.GetIl2CppMethodByToken(Il2CppClassPointerStore<SkillReader>.NativeClassPtr, 100667529));
                long bas = (long)hb - 0xfa1fdc;                         // SkillReader.HasEquipBypass rva (docs/hooks.tsv)
                var want = new long[a.Length - 1]; var best = new long[want.Length]; var name = new string[want.Length];
                for (int i = 1; i < a.Length; i++) want[i - 1] = Convert.ToInt64(a[i], 16);
                IntPtr dom = IL2CPP.il2cpp_domain_get(); uint na = 0;
                IntPtr asms; unsafe { asms = (IntPtr)IL2CPP.il2cpp_domain_get_assemblies(dom, ref na); }
                for (uint i = 0; i < na; i++)
                {
                    IntPtr img = IL2CPP.il2cpp_assembly_get_image(Marshal.ReadIntPtr(asms, (int)i * IntPtr.Size));
                    uint nc = IL2CPP.il2cpp_image_get_class_count(img);
                    for (uint c = 0; c < nc; c++)
                    {
                        IntPtr k = IL2CPP.il2cpp_image_get_class(img, c); IntPtr it = IntPtr.Zero, mi;
                        while ((mi = IL2CPP.il2cpp_class_get_methods(k, ref it)) != IntPtr.Zero)
                        {
                            long r = (long)Marshal.ReadIntPtr(mi) - bas;
                            for (int j = 0; j < want.Length; j++)
                                if (r <= want[j] && r > best[j])
                                {
                                    best[j] = r;
                                    name[j] = Marshal.PtrToStringUTF8(IL2CPP.il2cpp_class_get_namespace(k)) + "." + Marshal.PtrToStringUTF8(IL2CPP.il2cpp_class_get_name(k))
                                              + "." + Marshal.PtrToStringUTF8(IL2CPP.il2cpp_method_get_name(mi));
                                }
                        }
                    }
                }
                for (int j = 0; j < want.Length; j++) L.LogInfo($"zetu-probe: who: 0x{want[j]:x} in {name[j]} (starts 0x{best[j]:x})");
                return;
            }
            if (cmd == "bypass") { L.LogInfo($"zetu-probe: bypass: yes {bypassTrue} no {bypassFalse}"); return; }
            if (cmd.StartsWith("notice"))
            {
                var o = UObject.FindObjectOfType(Il2CppType.Of<CommonNoticeView>(), true);
                var v = o != null ? o.TryCast<CommonNoticeView>() : null;
                bool shown = v != null && v.gameObject.activeInHierarchy;
                if (cmd == "notice")
                {
                    L.LogInfo($"zetu-probe: notice: shown {shown}; text [{(v?._noticeText != null ? v._noticeText.text : "").Replace('\n', '|')}]; " +
                              $"ok [{v?._txtConfirm?.text}] cancel [{(v?._cancelButton != null && v._cancelButton.gameObject.activeSelf ? v._txtCancle?.text : "-")}]");
                    return;
                }
                if (!shown) { L.LogInfo($"zetu-probe: {cmd}: no notice shown"); return; }
                if (cmd == "notice_ok") v.OnClick_Confirm(); else v.OnClick_Cancle();
                L.LogInfo($"zetu-probe: {cmd}: clicked");
                return;
            }
            if (cmd == "risks")
            {
                var o = UObject.FindObjectOfType(Il2CppType.Of<SubUI_CheckInventoryPanelUI>());
                var v = o != null ? o.TryCast<SubUI_CheckInventoryPanelUI>() : null;
                var l = v != null ? v._riskRows : null; int n = l != null ? l.Count : 0, shown = 0;
                for (int i = 0; i < n; i++)
                {
                    var t = l[i]; if (t == null || !t.gameObject.activeSelf) continue;
                    L.LogInfo($"risk row {i}: {t.text}"); shown++;
                }
                L.LogInfo($"zetu-probe: risks: {shown} shown of {n} rows{(v == null ? " (no supplies step open)" : "")}");
                return;
            }
            var mf = UObject.FindObjectOfType<MainFlow>();
            var q = mf != null ? mf._eventQueue : null;
            if (q == null) { L.LogInfo("zetu-probe: no MainFlow / EventQueue"); return; }
            switch (cmd)
            {
                case "select_all": Send(q, new SelectAllUnitRequested(), line); return;
                case "result": Send(q, new ShowDungeonResultRequested(), line); return;
                case "toggle": Send(q, new ToggleEntityUIRequested((StaticUIType)int.Parse(a[1])), line); return;
                case "unequip":
                    Send(q, new UnequipItemRequested((MapType)int.Parse(a[1]), Il2CppSystem.Guid.Parse(a[2]), 0, new Il2CppSystem.Nullable<int>(1)), line);
                    return;
                case "unequip_all":
                    {
                        var m = (MapType)int.Parse(a[1]); var us = mf._inMapDatas[m].GetAllPlayerUnits(false); int k = 0;
                        for (int i = 0; i < us.Count; i++)
                        {
                            var u = us[i]; if (u == null) continue;
                            q.AddEvent(new UnequipItemRequested(m, u._Guid_k__BackingField, 0, new Il2CppSystem.Nullable<int>(1)).Cast<IEventData>()); k++;
                        }
                        L.LogInfo($"zetu-probe: {line}: {k} units");
                        return;
                    }
                case "approach":
                    {
                        var m = (MapType)int.Parse(a[1]); var d = mf._inMapDatas[m]; var tfh = mf._tickFlowHandlers[m];
                        var ps = d.GetAllPlayerUnits(false);
                        if (ps.Count == 0) { L.LogInfo($"zetu-probe: {line}: no player unit"); return; }
                        var g0 = ps[0]._Guid_k__BackingField; var p0 = d.GetLogicalPosition(g0);
                        var all = d.GetAliveUnitsOnMap(); UnitEntity best = null; int bd = int.MaxValue;
                        for (int i = 0; i < all.Count; i++)
                        {
                            var u = all[i]; if (u == null || !d.IsHostileTo(g0, u._Guid_k__BackingField)) continue;
                            var q2 = d.GetLogicalPosition(u._Guid_k__BackingField); int dd = Math.Abs(q2.x - p0.x) + Math.Abs(q2.y - p0.y);
                            if (dd < bd) { bd = dd; best = u; }
                        }
                        if (best == null) { L.LogInfo($"zetu-probe: {line}: no hostile unit"); return; }
                        var to = d.GetLogicalPosition(best._Guid_k__BackingField);
                        for (int i = 0; i < ps.Count; i++)
                            tfh.EnqueueEvent(new DirectOrderRequested(ps[i].Cast<IEntity>(), new Refactor.Task.MoveTask(to, 0, 0)).Cast<IEventData>(), false);
                        L.LogInfo($"zetu-probe: {line}: {ps.Count} units to {best.GetNameTextKey()} at {to.x},{to.y} (distance {bd})");
                        return;
                    }
                case "cast":
                    {
                        var m = (MapType)int.Parse(a[1]); var d = mf._inMapDatas[m]; var ug = Il2CppSystem.Guid.Parse(a[2]);
                        var p0 = d.GetLogicalPosition(ug); var all = d.GetAliveUnitsOnMap(); UnitEntity best = null; int bd = int.MaxValue;
                        for (int i = 0; i < all.Count; i++)
                        {
                            var u = all[i]; if (u == null || !d.IsHostileTo(ug, u._Guid_k__BackingField)) continue;
                            var q2 = d.GetLogicalPosition(u._Guid_k__BackingField); int dd = Math.Abs(q2.x - p0.x) + Math.Abs(q2.y - p0.y);
                            if (dd < bd) { bd = dd; best = u; }
                        }
                        if (best == null) { L.LogInfo($"zetu-probe: {line}: no hostile unit"); return; }
                        var none = new Il2CppSystem.Nullable<Il2CppSystem.Guid>(IL2CPP.il2cpp_object_new(Il2CppClassPointerStore<Il2CppSystem.Nullable<Il2CppSystem.Guid>>.NativeClassPtr));
                        q.AddEvent(new UseSkillTargetRequested(m, ug, best._Guid_k__BackingField, a[3], none).Cast<IEventData>());
                        L.LogInfo($"zetu-probe: {line}: sent (target {best.GetNameTextKey()}, distance {bd})");
                        return;
                    }
                case "units_list":
                    {
                        var us = mf._inMapDatas[(MapType)int.Parse(a[1])].GetAllPlayerUnits(false); string t = "";
                        for (int i = 0; i < us.Count; i++) t += " " + us[i]._Guid_k__BackingField.ToString();
                        L.LogInfo($"zetu-probe: units_list {a[1]}:{t}");
                        return;
                    }
                case "units":
                    {
                        string t = "";
                        foreach (var m in new[] { MapType.Base, MapType.Dungeon })
                        {
                            int n = -1;
                            try { n = mf._inMapDatas[m].GetAllPlayerUnits(false).Count; } catch { }
                            t += $" {m} {n}";
                        }
                        L.LogInfo($"zetu-probe: units:{t}");
                        return;
                    }
                case "speed":
                    {
                        var gs = mf._campaignSetting?.GameSpeedSetting;
                        if (gs == null) { L.LogInfo("zetu-probe: speed: no setting"); return; }
                        string pr = "";
                        var pd = gs._pauseReasons;
                        foreach (GamePauseReason r in Enum.GetValues(typeof(GamePauseReason)))
                            if (pd != null && pd.ContainsKey(r) && pd[r] != 0) pr += $" {r}={pd[r]}"; else if (gs.HasPauseReason(r)) pr += " " + r;
                        L.LogInfo($"zetu-probe: speed: {gs._gameSpeed} requested {gs._requestedGameSpeed} lock {gs._gameSpeedLockCount} pause [{pr.Trim()}]");
                        return;
                    }
                case "speed_clear": mf._campaignSetting.GameSpeedSetting.RemovePauseReasonCount(GamePauseReason.PopupTimeLock); L.LogInfo($"zetu-probe: {line}: done"); return;
                case "set_speed": Send(q, new SetGameSpeedRequested((GameSpeed)int.Parse(a[1])), line); return;
                case "speed_unlock": Send(q, new GameSpeedLockRequested(GameSpeedLock.Unlock), line); return;
                case "load": Send(q, new LoadCampaignRequested(line.Substring(5).Trim()), line); return;
                case "recruit":
                    {
                        var clan = mf._campaignData?.ClanDataContainer; var rh = mf._campaignHelperSet?._recruitHelper;
                        if (clan == null || rh == null) { L.LogInfo("zetu-probe: recruit: no clan / helper"); return; }
                        bool refreshed = rh.ForceRefreshRecruitCandidates();
                        var cand = clan._AvailableRecruitCandidates_k__BackingField;
                        L.LogInfo($"zetu-probe: recruit: refreshed {refreshed}, candidates {(cand != null ? cand.Count : -1)}, gold {clan._ClanGold_k__BackingField}, " +
                                  $"population {clan._CurrentPopulation_k__BackingField}/{clan._MaxPopulation_k__BackingField}");
                        if (clan._ClanGold_k__BackingField < 100000) clan._ClanGold_k__BackingField = 100000;
                        if (clan._MaxPopulation_k__BackingField <= clan._CurrentPopulation_k__BackingField) clan._MaxPopulation_k__BackingField = clan._CurrentPopulation_k__BackingField + 1;
                        var ms = SystemVar.Campaign.MapSizeMax;
                        var pos = new Vector2Int(ms.x / 2, ms.y / 2 - 2);
                        Send(q, new CandidateRecruitRequested(0, MapType.Base, pos, FactionType.Player), line);
                        return;
                    }
            }
            L.LogInfo($"zetu-probe: unknown request '{line}'");
        }
    }
}
