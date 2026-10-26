// The screens of an expedition with more than 4 units (zetu_ui.c). Only patched when max_units != 4.
// The stock prefabs hold 4 of each per-unit element, placed by hand (the result screen: a grid); the game's loops stop at the
// list / array size, so without this units 5..N are simply missing (no crash). Here the 4th element is copied until
// there are N, the copies go into the game's own list / array (so the stock code fills, shows and wires them like the
// first 4), and the row is laid out again at a smaller scale inside the width the 4 stock elements used:
//   - supplies step: SubUI_CheckInventoryPanelUI._panels (EntityInventoryPanelUI[]), before ExpeditionPlanView.Init
//     (Init wires the callbacks of every panel of the array, by index), and the empty-slot boxes behind them;
//   - region step, the selected units' portraits: SubUI_SelectRegionUI._bottomtransferUnitImages and
//     _bottomtransferUnitSelectedOutlines (List<Image>), same time;
//   - region step, the count "Expedition Members (n/4)": the "/4" is a literal of SubUI_SelectRegionUI.SetUnit, and every
//     call of SetUnit is followed by SubUI_ExpeditionPlanLowerPanel.OnSetUnit, so the text is rewritten to "/N" after that;
//   - result screen: DungeonResultView._portraitUIList (List<SubUI_DungeonResultPortrait>), before SetPlayerData; its
//     GridLayoutGroup row is made one line long and scaled (the view is made again for each opening);
//   - the Inventory / Status / Skill windows of selected units (see "unit windows").
using System;
using System.Collections.Generic;
using HarmonyLib;
using Il2CppInterop.Runtime.InteropTypes;
using Il2CppInterop.Runtime.InteropTypes.Arrays;
using Refactor.UI;
using TMPro;
using UnityEngine;
using UnityEngine.UI;
using UObject = UnityEngine.Object;

namespace ZetuSuite
{
    static class Screens
    {
        static int Max => Plugin.MaxUnits;
        static void Log(string s) => Plugin.L.LogInfo(s);

        internal static bool Install(Harmony h)
        {
            var o = typeof(Screens);
            bool ok = Plugin.Patch(h, o, typeof(EntityInventoryUI), "Init", 1, prefix: nameof(InvInit))
                && Plugin.Patch(h, o, typeof(EntityStatusUI), "Init", 0, prefix: nameof(StatusInit))
                && Plugin.Patch(h, o, typeof(EntitySkillUI), "InitUI", 2, prefix: nameof(SkillInit))
                && Plugin.Patch(h, o, typeof(EntityInventoryPresenter), "SetUnitUI", 0, postfix: nameof(InvSet))
                && Plugin.Patch(h, o, typeof(EntityStatusUIPresenter), "RefreshSelectedEntityUI", 1, postfix: nameof(StatusRefresh))
                && Plugin.Patch(h, o, typeof(EntitySkillUIPresenter), "RefreshSelectedEntityUI", 1, prefix: nameof(SkillRefreshPre), postfix: nameof(SkillRefresh))
                && Plugin.Patch(h, o, typeof(ExpeditionPlanView), "Init", 8, prefix: nameof(PlanInit))
                && Plugin.Patch(h, o, typeof(SubUI_ExpeditionPlanLowerPanel), "OnSetUnit", 1, postfix: nameof(LowerSetUnit))
                // 4 parameters on build 25846389 (+ missingUnitNameKeys), 3 on the older builds; ResultPost reads only the two
                // lists both have (Harmony binds them by name)
                && Plugin.Patch(h, o, typeof(DungeonResultView), "SetPlayerData", 4, prefix: nameof(ResultPre), postfix: nameof(ResultPost), orArgc: 3);
            if (ok) Log($"expedition UI: {Max} unit panels, portraits and result portraits; count text /{Max}; unit windows {Max} panels");
            return ok;
        }

        // ------------------------------------------------------------------------------------------------ helpers
        static bool Alive(UObject o) => o != null && (bool)o;
        static Transform Tr(Component c) => Alive(c) ? c.transform : null;
        static Transform Parent(Transform t) => Alive(t) ? t.parent : null;
        static RectTransform RT(Transform t) => Alive(t) ? t.TryCast<RectTransform>() : null;
        static Vector2 Pos(Transform t) { var r = RT(t); return r != null ? r.anchoredPosition : Vector2.zero; }
        static void SetPos(Transform t, Vector2 p) { var r = RT(t); if (r != null) r.anchoredPosition = p; }
        static Vector2 Size(Transform t) { var r = RT(t); return r != null ? r.sizeDelta : Vector2.zero; }
        static Vector2 Pivot(Transform t) { var r = RT(t); return r != null ? r.pivot : new Vector2(0.5f, 0.5f); }
        static Vector2 AMin(Transform t) { var r = RT(t); return r != null ? r.anchorMin : Vector2.zero; }
        static Rect RectOf(Transform t) { var r = RT(t); return r != null ? r.rect : default; }
        static float Scale(Transform t) => Alive(t) ? t.localScale.x : 1;
        static void SetScale(Transform t, float s) { if (Alive(t)) t.localScale = new Vector3(s, s, 1); }
        static bool Active(Component c) => Alive(c) && c.gameObject.activeSelf;
        static void Rebuild(Transform t) { var r = RT(t); if (r != null) LayoutRebuilder.ForceRebuildLayoutImmediate(r); }

        /* The slot of element e: its ancestor that is a direct child of `row` (the common parent of the 4 stock elements).
           path: the names from the slot down to e ("" = e is on the slot itself). */
        static Transform SlotOf(Component e, Transform row, out string path)
        {
            path = "";
            var names = new List<string>();
            var t = Tr(e);
            while (t != null && names.Count < 16) { var p = Parent(t); if (p != null && p == row) break; names.Add(t.name); t = p; }
            if (t == null || names.Count >= 16) return null;
            names.Reverse();
            path = string.Join("/", names);
            return t;
        }
        static Transform RowOf(Component a, Component b)                // lowest common ancestor of two elements
        {
            for (var p = Parent(Tr(a)); p != null; p = Parent(p))
                for (var q = Parent(Tr(b)); q != null; q = Parent(q))
                    if (p == q) return p;
            return null;
        }
        static Transform Find(Transform slot, string path) => path.Length == 0 ? slot : (Alive(slot) ? slot.Find(path) : null);
        /* a copy of `slot` under the same parent, at sibling index sib (drawn like the stock ones) */
        static Transform Copy(Transform slot, int sib)
        {
            var c = UObject.Instantiate(slot, slot.parent)?.TryCast<Transform>();
            if (c != null && sib >= 0) c.SetSiblingIndex(sib);
            return c;
        }

        /* Lay out n slots (the first `stock` placed by the prefab) in one row with the stock row's span (left edge of the first
           to right edge of the last stock slot) and top edge: smaller, same order. Every rect is taken in the row's own space
           (x from its left edge, y from its bottom edge). A GridLayoutGroup row (the result screen) is made one line long
           and scaled instead. overlap = true when two slots would overlap. */
        static float LayoutRow(Transform[] slot, int n, int stock, out bool overlap, out string info)
        {
            var row = Parent(slot[0]);
            overlap = false;
            if (row.GetComponent<GridLayoutGroup>() != null)
            {
                Rebuild(row);
                float x0 = Pos(slot[0]).x, dx = Pos(slot[1]).x - x0;
                Vector2 rs = Size(row), rp = Pos(row), pv = Pivot(row); float sc = Scale(row);
                if (!(dx > 0)) { info = $"grid pitch {dx:F1}: not laid out"; overlap = true; return 0; }
                float s = (float)stock / n, w2 = rs.x + (n - stock) * dx;
                float cOld = rp.x - pv.x * rs.x * sc + sc * (x0 + (stock - 1) * dx / 2);
                RT(row).sizeDelta = new Vector2(w2, rs.y);
                SetScale(row, sc * s);
                SetPos(row, new Vector2(cOld + pv.x * w2 * sc * s - sc * s * (x0 + (n - 1) * dx / 2), rp.y));
                Rebuild(row);
                for (int i = 1; i < n; i++)
                {
                    Vector2 a = Pos(slot[i - 1]), b = Pos(slot[i]);
                    if (b.y != a.y || b.x - a.x < dx - 0.5f) overlap = true;
                }
                info = $"grid, pitch {dx:F0}, {n} in one line, row {w2:F0} wide at scale {sc * s:F3}";
                return s;
            }
            Rect rr = RectOf(row);
            float[] L = new float[8], T = new float[8]; float w = 0, h = 0;
            for (int i = 0; i < stock; i++)
            {
                Vector2 a = AMin(slot[i]), p = Pos(slot[i]), pv = Pivot(slot[i]), sz = Size(slot[i]); float sc = Scale(slot[i]);
                L[i] = a.x * rr.width + p.x - pv.x * sz.x * sc;
                T[i] = a.y * rr.height + p.y + (1 - pv.y) * sz.y * sc;
                if (i == 0) { w = sz.x * sc; h = sz.y * sc; }
            }
            float dxs = stock > 1 ? (L[stock - 1] - L[0]) / (stock - 1) : w;
            if (!(dxs > 0) || !(w > 0)) { info = $"stock pitch {dxs:F1} / width {w:F1}: not laid out"; overlap = true; return 0; }
            float span = (stock - 1) * dxs + w, k = span / ((n - 1) * dxs + w);
            if (k > 1) k = 1;
            for (int i = 0; i < n; i++)
            {
                Vector2 a = AMin(slot[i]), pv = Pivot(slot[i]), sz = Size(slot[i]);
                float sc1 = Scale(slot[i]) * k;                         // a copy: the size and scale of the slot it copies
                SetScale(slot[i], sc1);
                SetPos(slot[i], new Vector2(L[0] + i * dxs * k - a.x * rr.width + pv.x * sz.x * sc1, T[0] - a.y * rr.height - (1 - pv.y) * sz.y * sc1));
            }
            float prevR = -1e9f;                                        // check: the placed rects, left to right
            for (int i = 0; i < n; i++)
            {
                Vector2 a = AMin(slot[i]), p = Pos(slot[i]), pv = Pivot(slot[i]), sz = Size(slot[i]); float sc = Scale(slot[i]);
                float l = a.x * rr.width + p.x - pv.x * sz.x * sc;
                if (l < prevR - 0.5f) overlap = true;
                prevR = l + sz.x * sc;
            }
            info = $"stock {stock} from x {L[0]:F0}, pitch {dxs:F0}, {w:F0}x{h:F0}; now {n} at scale {k:F3}, x {L[0]:F0}..{prevR:F0} of {rr.width:F0}";
            return k;
        }

        // ---------------------------------------------------------------------------------------- supplies + portraits
        static void GrowPanels(ExpeditionPlanView view)
        {
            var cip = view._checkInventoryPanelUI;
            var arr = cip != null ? cip._panels : null;
            int n = arr != null ? arr.Length : 0;
            if (n >= Max) return;                                       // already grown (the view is pooled)
            if (n < 2) { Log($"supplies: {n} stock panels, left as is"); return; }
            var e = new EntityInventoryPanelUI[Max]; var slot = new Transform[Max];
            for (int i = 0; i < n; i++) e[i] = arr[i];
            var row = RowOf(e[0], e[1]);
            string path = "";
            for (int i = 0; i < n; i++) { slot[i] = SlotOf(e[i], row, out var p); if (i == n - 1) path = p; }
            if (row == null || slot[n - 1] == null) { Log("supplies: panel row not found"); return; }
            int sib = slot[n - 1].GetSiblingIndex();
            var na = new Il2CppReferenceArray<EntityInventoryPanelUI>(Max);
            for (int i = 0; i < n; i++) na[i] = e[i];
            for (int i = n; i < Max; i++)
            {
                slot[i] = Copy(slot[n - 1], ++sib);
                var pe = slot[i] != null ? Find(slot[i], path) : null;
                e[i] = pe != null ? pe.GetComponent<EntityInventoryPanelUI>() : null;
                if (e[i] == null) { Log($"supplies: copy {i} has no EntityInventoryPanelUI (path '{path}')"); return; }
                na[i] = e[i];
            }
            cip._panels = na;
            LayoutRow(slot, Max, n, out bool ov, out string info);
            Log($"supplies: {Max} unit panels in {row.name} ({n} stock + {Max - n} copies), one row: {info}, {(ov ? "OVERLAP" : "no overlap")}");
            /* the empty-slot boxes behind the panels ("Not Selected", Panel_InventoriesBG: decoration, one per stock panel) */
            var bgrow = Find(Parent(row), "Panel_InventoriesBG");
            int nb = bgrow != null ? bgrow.childCount : 0;
            if (nb == n)
            {
                var bg = new Transform[Max];
                for (int i = 0; i < n; i++) bg[i] = bgrow.GetChild(i);
                for (int i = n; i < Max; i++) bg[i] = Copy(bg[n - 1], i);
                LayoutRow(bg, Max, n, out ov, out info);
                Log($"supplies: {Max} empty-slot boxes, one row: {info}, {(ov ? "OVERLAP" : "no overlap")}");
            }
            else Log($"supplies: Panel_InventoriesBG has {nb} boxes (not {n}): left as is");
        }

        /* the selected units' portraits of the region step: images and their outlines, same slots or not */
        static void GrowPortraits(SubUI_SelectRegionUI sel)
        {
            var imgs = sel._bottomtransferUnitImages; var lines = sel._bottomtransferUnitSelectedOutlines;
            int n = imgs != null ? imgs.Count : 0, nl = lines != null ? lines.Count : 0;
            if (n >= Max) return;
            if (n < 2 || nl != n) { Log($"portraits: {n} images / {nl} outlines, left as is"); return; }
            var row = RowOf(imgs[0], imgs[1]);
            var slot = new Transform[Max]; string pi = "", pl = "";
            for (int i = 0; i < n; i++) { slot[i] = SlotOf(imgs[i], row, out var p); if (i == n - 1) pi = p; }
            var lslot = SlotOf(lines[n - 1], row, out pl);              // outline in the same slot as its image?
            bool shared = lslot != null && lslot == slot[n - 1];
            var lrow = shared ? row : RowOf(lines[0], lines[1]);
            var ls = new Transform[Max];
            if (!shared) for (int i = 0; i < n; i++) { ls[i] = SlotOf(lines[i], lrow, out var p); if (i == n - 1) pl = p; }
            if (row == null || slot[n - 1] == null || (!shared && ls[n - 1] == null)) { Log("portraits: row not found"); return; }
            int sib = slot[n - 1].GetSiblingIndex(), lsib = shared ? 0 : ls[n - 1].GetSiblingIndex();
            for (int i = n; i < Max; i++)
            {
                slot[i] = Copy(slot[n - 1], ++sib);
                if (!shared) { ls[i] = Copy(ls[n - 1], ++lsib); if (lrow == row) sib++; }
                var im = slot[i] != null ? Find(slot[i], pi)?.GetComponent<Image>() : null;
                var ln = shared ? Find(slot[i], pl)?.GetComponent<Image>() : (ls[i] != null ? Find(ls[i], pl)?.GetComponent<Image>() : null);
                if (im == null || ln == null) { Log($"portraits: copy {i} incomplete (image '{pi}', outline '{pl}')"); return; }
                imgs.Add(im); lines.Add(ln);
            }
            LayoutRow(slot, Max, n, out bool ov, out string info);
            bool ov2 = false; string info2 = "";
            if (!shared) LayoutRow(ls, Max, n, out ov2, out info2);
            Log($"portraits: {imgs.Count} ({n} stock + {Max - n} copies), outlines {(shared ? "in the same slots" : "own slots")}; one row: {info}" +
                $"{(shared ? "" : "; outlines: ")}{info2}, {(ov || ov2 ? "OVERLAP" : "no overlap")}");
        }

        static TMP_Text selText;                                        // the count text of the region step
        static void PlanInit(ExpeditionPlanView __instance)
        {
            try
            {
                GrowPanels(__instance);                                 // copies first: the stock Init wires every panel
                var sel = __instance._selectRegionUI;
                if (sel != null) GrowPortraits(sel);
                selText = sel != null ? sel._txtTransferUnitInfo : null;
            }
            catch (Exception e) { Plugin.L.LogError("expedition window: " + e); }
        }

        /* "(n/4)" -> "(n/N)" in the count text of the region step, after every SetUnit (LowerPanel.OnSetUnit follows each) */
        static int countMiss, countSaid, selSaid;
        static void FixCountText()
        {
            if (!Alive(selText)) { if (countMiss++ < 3) Log("count text: not found"); return; }
            string s = selText.text;
            if (s == null || s.Length < 3 || s.Length > 500) return;
            string from = $"/{Party.Stock})";
            int i = s.LastIndexOf(from, StringComparison.Ordinal);
            if (i < 0) { if (countMiss++ < 3) Log($"count text: no \"{from}\" in it ({s.Length} chars)"); return; }
            s = s.Substring(0, i) + $"/{Max})" + s.Substring(i + from.Length);
            selText.text = s;
            if (countSaid++ < 40) Log($"count text now '{s}'");
        }
        static void LowerSetUnit(int count)
        {
            try { FixCountText(); } catch (Exception e) { Plugin.L.LogError("count text: " + e); }
            if (selSaid++ < 40) Log($"expedition: {count} selected");
        }

        // ------------------------------------------------------------------------------------------------ result screen
        static void ResultPre(DungeonResultView __instance)
        {
            try
            {
                var l = __instance._portraitUIList;
                int n = l != null ? l.Count : 0;
                if (n >= Max || n < 2) return;
                var row = RowOf(l[0], l[1]);
                var slot = new Transform[Max]; string path = "";
                for (int i = 0; i < n; i++) { slot[i] = SlotOf(l[i], row, out var p); if (i == n - 1) path = p; }
                if (row == null || slot[n - 1] == null) { Log("result: portrait row not found"); return; }
                int sib = slot[n - 1].GetSiblingIndex();
                for (int i = n; i < Max; i++)
                {
                    slot[i] = Copy(slot[n - 1], ++sib);
                    var p = slot[i] != null ? Find(slot[i], path)?.GetComponent<SubUI_DungeonResultPortrait>() : null;
                    if (p == null) { Log($"result: copy {i} has no SubUI_DungeonResultPortrait"); return; }
                    l.Add(p);
                }
                LayoutRow(slot, Max, n, out bool ov, out string info);
                Log($"result: {l.Count} portraits ({n} stock + {Max - n} copies), one row: {info}, {(ov ? "OVERLAP" : "no overlap")}");
            }
            catch (Exception e) { Plugin.L.LogError("result screen: " + e); }
        }
        static void ResultPost(DungeonResultView __instance, Il2CppSystem.Collections.Generic.List<Refactor.UnitEntity> playerUnits,
                               Il2CppSystem.Collections.Generic.List<Refactor.UnitEntity> deadUnits)
        {
            try
            {
                var l = __instance._portraitUIList; int shown = 0, n = l != null ? l.Count : 0;
                int used = (playerUnits != null ? playerUnits.Count : 0) + (deadUnits != null ? deadUnits.Count : 0);
                for (int i = Party.Stock; i < n; i++)                   // the copies: shown for the units the stock loop filled
                    if (Alive(l[i])) l[i].gameObject.SetActive(i < used);
                for (int i = 0; i < n; i++) if (Active(l[i])) shown++;
                Log($"result: {(playerUnits != null ? playerUnits.Count : 0)} units + {(deadUnits != null ? deadUnits.Count : 0)} dead, {shown} portraits shown");
            }
            catch (Exception e) { Plugin.L.LogError("result screen: " + e); }
        }

        /* ------------------------------------------------------------------------- unit windows: Inventory, Status, Skill
           Each view holds 4 panels (EntityInventoryUI / EntityStatusUI / EntitySkillUI._panels) in a HorizontalLayoutGroup row;
           its presenter fills panel i with selected unit i, up to PlayerPartyMax, and blanks the rest. The panel copies go
           into _panels before the view's Init (Init / InitUI wire every panel of the array); the layout group places them.
           After each refresh the row is fitted: the panels of the shown units keep the stock top-left corner and the row is
           scaled down (only) so its right edge stays inside the screen with the stock margin. */
        class Win
        {
            public string Name, Panel;
            public Func<Il2CppObjectBase, Component[]> Get;             // the view's panels
            public Func<Il2CppObjectBase, Transform, int, bool> Grow;   // copies of the last panel into a new array
            public Il2CppObjectBase View; public IntPtr ViewPtr;
            public float TlX, TlY, LastS; public bool HaveTl;
        }
        static Component[] Arr<T>(Il2CppReferenceArray<T> a) where T : Component
        {
            if (a == null) return Array.Empty<Component>();
            var r = new Component[a.Length]; for (int i = 0; i < r.Length; i++) r[i] = a[i]; return r;
        }
        static bool GrowArr<T>(Il2CppReferenceArray<T> old, Transform last, Action<Il2CppReferenceArray<T>> set, string view) where T : Component
        {
            int n = old.Length, sib = last.GetSiblingIndex();
            var na = new Il2CppReferenceArray<T>(Max);
            for (int i = 0; i < n; i++) na[i] = old[i];
            for (int i = n; i < Max; i++)
            {
                var c = Copy(last, ++sib); var p = c != null ? c.GetComponent<T>() : null;
                if (p == null) { Log($"{view}: copy {i} has no {typeof(T).Name}"); return false; }
                na[i] = p;
            }
            set(na);
            return true;
        }
        static readonly Win[] W =
        {
            new Win { Name = "EntityInventoryUI", Panel = "EntityInventoryPanelUI",
                Get = v => Arr(v.Cast<EntityInventoryUI>()._panels),
                Grow = (v, last, n) => { var x = v.Cast<EntityInventoryUI>(); return GrowArr(x._panels, last, a => x._panels = a, "EntityInventoryUI"); } },
            new Win { Name = "EntityStatusUI", Panel = "EntityStatusPanelUI",
                Get = v => Arr(v.Cast<EntityStatusUI>()._panels),
                Grow = (v, last, n) => { var x = v.Cast<EntityStatusUI>(); return GrowArr(x._panels, last, a => x._panels = a, "EntityStatusUI"); } },
            new Win { Name = "EntitySkillUI", Panel = "EntitySkillPanelUI",
                Get = v => Arr(v.Cast<EntitySkillUI>()._panels),
                Grow = (v, last, n) => { var x = v.Cast<EntitySkillUI>(); return GrowArr(x._panels, last, a => x._panels = a, "EntitySkillUI"); } },
        };
        static void GrowWindow(int w, Il2CppObjectBase view)
        {
            var z = W[w];
            if (z.ViewPtr != view.Pointer) { z.View = view; z.ViewPtr = view.Pointer; z.HaveTl = false; }   // a new view (one per map load)
            var arr = z.Get(view); int n = arr.Length;
            if (n >= Max) return;                                       // grown (InitUI runs more than once)
            if (n < 2) { Log($"{z.Name}: {n} panels, left as is"); return; }
            Transform last = Tr(arr[n - 1]), row = Parent(last);
            if (row == null || Parent(Tr(arr[0])) != row) { Log($"{z.Name}: panels not in one row, left as is"); return; }
            if (z.Grow(view, last, n)) Log($"{z.Name}: {Max} panels ({n} stock + {Max - n} copies)");
        }
        static readonly Il2CppStructArray<Vector3> corners = new Il2CppStructArray<Vector3>(4);
        static Vector3[] Corners(Transform t)                           // world corners = screen pixels (overlay canvas)
        {
            RT(t).GetWorldCorners(corners);
            return new[] { corners[0], corners[1], corners[2], corners[3] };
        }
        static void FitWindow(int w)
        {
            var z = W[w];
            var view = z.View?.TryCast<UObject>();
            if (!Alive(view)) return;
            var arr = z.Get(z.View); int n = arr.Length, k = -1;
            if (n < 2) return;
            Transform p0 = Tr(arr[0]), row = Parent(p0);
            for (int i = 0; i < n; i++) if (Active(arr[i])) k = i;
            if (row == null || k < 0) return;
            Rebuild(row);
            float sc = Scale(row);
            var c0 = Corners(p0); var ck = Corners(Tr(arr[k]));
            if (!z.HaveTl) { z.TlX = c0[1].x; z.TlY = c0[1].y; z.HaveTl = true; z.LastS = float.NaN; }   // the stock corner; the first fit is logged
            float w1 = (ck[2].x - c0[0].x) / sc;                         // the shown panels' width at scale 1, in pixels
            float sw = Screen.width;
            float margin = z.TlX > 8 ? z.TlX : 8, avail = sw - z.TlX - margin;
            float s = w1 > avail && w1 > 0 ? avail / w1 : 1;
            SetScale(row, s);
            Rebuild(row);
            c0 = Corners(p0);                                           // back to the stock top-left corner
            float ps = Parent(row).lossyScale.x;
            Vector2 pos = Pos(row);
            if (ps > 0) SetPos(row, new Vector2(pos.x + (z.TlX - c0[1].x) / ps, pos.y + (z.TlY - c0[1].y) / ps));
            if (s != z.LastS)
            {
                c0 = Corners(p0); ck = Corners(Tr(arr[k]));
                Log($"{z.Name}: {k + 1} units shown, row scale {s:F3}, x {c0[0].x:F0}..{ck[2].x:F0} of {sw:F0} px, top {c0[1].y:F0}");
                z.LastS = s;
            }
        }
        static void Guard(Action a, string what) { try { a(); } catch (Exception e) { Plugin.L.LogError(what + ": " + e); } }
        static void InvInit(EntityInventoryUI __instance) => Guard(() => GrowWindow(0, __instance), "EntityInventoryUI");
        static void StatusInit(EntityStatusUI __instance) => Guard(() => GrowWindow(1, __instance), "EntityStatusUI");
        static void SkillInit(EntitySkillUI __instance) => Guard(() => GrowWindow(2, __instance), "EntitySkillUI");
        /* presenters: fill the panels (now up to N), then fit the row */
        static void InvSet() => Guard(() => FitWindow(0), "EntityInventoryUI fit");
        static void StatusRefresh() => Guard(() => FitWindow(1), "EntityStatusUI fit");
        static void SkillRefreshPre(EntitySkillUIPresenter __instance)
        {
            /* the presenter's int[] _skillTreeIndices (selected tree per panel) is sized by PlayerPartyMax in its ctor */
            Guard(() =>
            {
                var ia = __instance._skillTreeIndices; int n = ia != null ? ia.Length : 0;
                if (n >= Max) return;
                var na = new Il2CppStructArray<int>(Max);
                for (int i = 0; i < n; i++) na[i] = ia[i];
                __instance._skillTreeIndices = na;
                Log($"EntitySkillUIPresenter: skill tree indices {n} -> {Max}");
            }, "EntitySkillUIPresenter");
        }
        static void SkillRefresh() => Guard(() => FitWindow(2), "EntitySkillUI fit");
    }
}
