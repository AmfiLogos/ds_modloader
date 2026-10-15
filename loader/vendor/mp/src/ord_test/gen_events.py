#!/usr/bin/env python3
"""Generate the Tier A event table for the orders lane (build 25846389).

Inputs: dump.cs (field offsets + line numbers), script.json (TypeInfo slots), il2cpp.h (field names),
handlers.txt-style sync/async map is not needed: the dylib picks sync/async at runtime.
Outputs: ../ord_events_gen.h, offsets_check.cpp (static_asserts against il2cpp.h), OFFSETS.md.
Run: python3 gen_events.py  (from ord_test/)."""
import json, re, os, sys

DUMP = os.path.expanduser('~/ds_skill_truth/dump_25846389')
HERE = os.path.dirname(os.path.abspath(__file__))

# stable wire ids: never renumber, only append (bump ORD_VERSION if a layout changes)
EVENTS = [
    (1, 'DirectTaskRequested'), (2, 'UseSkillTargetRequested'), (3, 'ToggleBehaviourOption'),
    (4, 'ToggleTaskOptionRequested'), (5, 'ToggleAutoSkillRequested'), (6, 'CarryUnitGetDownRequested'),
    (7, 'EquipItemRequested'), (8, 'UnequipItemRequested'), (9, 'TransferItemRequested'),
    (10, 'TransferFromEquipRequested'), (11, 'TransferEquipRequested'), (12, 'TransferAndEquipRequested'),
    (13, 'TransferStorageItemRequested'), (14, 'SplitDropItemRequested'), (15, 'SplitSendItemRequested'),
    (16, 'SplitMoveItemRequested'), (17, 'HalfSendItemRequested'), (18, 'DropItemRequested'),
    (19, 'ChangeItemSlotRequested'), (20, 'SortItemRequested'), (21, 'DisposeItemRequested'),
    (22, 'DisposeItemAmountRequested'), (23, 'AllowAutoFixItemRequested'), (24, 'BanAutoFixItemRequested'),
    (25, 'SelectAmmoRequested'), (26, 'ExpeditionDropItemRequested'), (27, 'ExpeditionTransferItemRequested'),
    (28, 'ExpeditionEquipFromGroundRequested'), (29, 'ExpeditionEquipFromInventoryRequested'),
    (30, 'SetItemQuickSlotRequested'), (31, 'UnsetItemQuickSlotRequested'),
    (32, 'ToggleHeadEquipmentVisibilityRequested'), (33, 'SetQuickSlotRequested'),
    (34, 'UnsetQuickSlotRequested'), (35, 'MoveQuickSlotRequested'), (36, 'SwapShortCutRequested'),
    (37, 'ToggleAutoPickUpRequested'), (38, 'PickUpWeightRuleChanged'), (39, 'SelectChoiceRequested'),
    (40, 'AlertClickRequested'), (41, 'AlertRightClickRequested'), (42, 'ChangeMapRequested'),
    (43, 'GoDownDungeonRequested'), (44, 'DungeonFloorClearRequested'), (45, 'ClearDungeonRegionRequested'),
    (46, 'SetSafeFloorSkipRequested'), (47, 'SetRecentlySelectedRegionRequested'),
    (48, 'RollLevelUpRequested'), (49, 'AcceptLevelUpRequested'), (50, 'LearnSkillRequested'),
    (51, 'TradeCommitRequested'),
    # ---- Set 2: base and town
    (52, 'CandidateRecruitRequested'), (53, 'RecruitCandidatesRefreshRequested'), (54, 'RetireUnitRequested'),
    (55, 'RenamePlayerUnitRequested'), (56, 'AssignBedOwnerRequested'), (57, 'CraftingTaskGenerateRequested'),
    (58, 'CraftingTaskRemoveRequested'), (59, 'CraftingOptionRequested'), (60, 'CraftingPendingRequested'),
    (61, 'CraftingRepeatRequested'), (62, 'CraftingPriorityRequested'), (63, 'AddFarmingRequested'),
    (64, 'ReduceFarmingRequested'), (65, 'ResearchTaskGenerateRequested'), (66, 'UnlockResearchRequested'),
    (67, 'ResearchProgressedRequested'), (68, 'WorkToggleRequested'), (69, 'WorkPriorityRequested'),
    (70, 'SetTaskPriorityRequested'), (71, 'SinglePolicyRequested'), (72, 'DurabilityPolicyRequested'),
    (73, 'AllowAllStoragePolicyRequested'), (74, 'BanAllStoragePolicyRequested'), (75, 'CopyPolicyRequested'),
    (76, 'PastePolicyRequested'), (77, 'IndividualPolicyRequested'), (78, 'BatchPolicyRequested'),
    (79, 'ResetPolicyRequested'), (80, 'UnitConsumePresetRequested'), (81, 'UnitConsumeCopyRequested'),
    (82, 'UnitConsumePasteRequested'), (83, 'UnitConsumeClearCopyRequested'),
    (84, 'RemoveFromPlayerOwnedItemRequested'), (85, 'ReplyLockConsecrationNoticeRequested'),
]
# swallowed and dropped in multiplayer (never sent)
BLOCKED = [(200, 'ToggleConsoleRequested'), (201, 'QuickLoadRequested'), (202, 'LoadCampaignRequested'),
           (203, 'SaveFileRequested')]
# per-event extras
FLAGS = {'DropItemRequested': 'ORD_EF_RAYCAST', 'TradeCommitRequested': 'ORD_EF_TRADE',
         'CandidateRecruitRequested': 'ORD_EF_RECRUIT',
         # handlers read the UI-only ExpeditionPlanPartyContext: the party travels with the order
         'ExpeditionDropItemRequested': 'ORD_EF_PARTY', 'ExpeditionTransferItemRequested': 'ORD_EF_PARTY',
         'ExpeditionEquipFromGroundRequested': 'ORD_EF_PARTY | ORD_EF_EQUIP',
         'ExpeditionEquipFromInventoryRequested': 'ORD_EF_PARTY | ORD_EF_EQUIP',
         # gear mode check before sending (host lobby option)
         'EquipItemRequested': 'ORD_EF_EQUIP', 'UnequipItemRequested': 'ORD_EF_EQUIP',
         'TransferFromEquipRequested': 'ORD_EF_EQUIP', 'TransferEquipRequested': 'ORD_EF_EQUIP',
         'TransferAndEquipRequested': 'ORD_EF_EQUIP',
         # handlers read ISelectReader: the owner check uses the first selected unit
         'ToggleBehaviourOption': 'ORD_EF_SELOWNER', 'ToggleTaskOptionRequested': 'ORD_EF_SELOWNER',
         'SelectAmmoRequested': 'ORD_EF_SELOWNER'}
# owner (unit the order is about) for the mp ownership check: first field found in this order
OWNER = ['UserGuid', 'UnitGuid', 'OwnerGuid', 'Owner', 'From', 'targetGuid', 'TargetGuid', 'Guid', 'SourceGuid', 'Target']

KIND = {'int': ('ORD_F_I32', 4), 'bool': ('ORD_F_U8', 1), 'Guid': ('ORD_F_GUID', 16),
        'Nullable<Guid>': ('ORD_F_NGUID', 20), 'Nullable<int>': ('ORD_F_NI32', 8),
        'Nullable<bool>': ('ORD_F_NU8', 2), 'string': ('ORD_F_STR', 8), 'IEntity': ('ORD_F_ENT', 8),
        'List<Guid>': ('ORD_F_LGUID', 8), 'TradeCommitRequest': ('ORD_F_TRADE', 8), 'Vector2Int': ('ORD_F_V2I', 8)}
ENUMS4 = {'MapType', 'TaskType', 'BehaviourOption', 'OfferingType', 'PickUpWeight', 'DungeonRegion',
          'MapTransitionVisualGuardMode', 'FactionType', 'TaskPriority', 'FoodPolicyPreset'}

lines = open(os.path.join(DUMP, 'dump.cs'), encoding='utf-8', errors='replace').read().split('\n')
sj = json.load(open(os.path.join(DUMP, 'script.json')))
ti = {}
for m in sj['ScriptMetadata']:
    n = m['Name']
    if n.endswith('_TypeInfo'):
        ti.setdefault(n[:-len('_TypeInfo')].split('.')[-1], []).append((m['Address'], n))

def find_class(name):
    """-> (decl line no (1-based), namespace, [(fieldline, type, fname, off)])"""
    pat = re.compile(r'^public (sealed )?class ' + re.escape(name) + r' : IEventData // TypeDefIndex')
    hits = [i for i, l in enumerate(lines) if pat.match(l)]
    if len(hits) != 1: sys.exit('class %s: %d hits' % (name, len(hits)))
    i = hits[0]
    ns = lines[i - 1].replace('// Namespace:', '').strip() if lines[i - 1].startswith('// Namespace') else \
        lines[i - 2].replace('// Namespace:', '').strip()
    fields = []
    j = i + 1
    while not lines[j].startswith('}'):
        m = re.match(r'\s*(?:public|private|internal|protected)\s+(?:readonly\s+)?(?!static|const)([\w<>.,\[\] ]+?)\s+([<>\w]+);\s*// 0x([0-9A-F]+)', lines[j])
        if m and 'static' not in lines[j] and 'const' not in lines[j]:
            fields.append((j + 1, m.group(1), m.group(2), int(m.group(3), 16)))
        j += 1
    return i + 1, ns, fields

def cname(f):  # il2cpp.h field name
    m = re.match(r'<(\w+)>k__BackingField', f)
    return '_%s_k__BackingField' % m.group(1) if m else f

out_h, out_cpp, out_md = [], [], []
out_h.append('/* GENERATED by ord_test/gen_events.py from dump_25846389 - do not edit. */')
out_h.append('#pragma once')
out_cpp.append('// GENERATED by gen_events.py: every event field offset used by ord_* checked against il2cpp.h')
out_cpp.append('#include "ord_il2cpp_pre.h"')
out_md.append('# Offsets used by the orders lane (build 25846389)\n')
out_md.append('Tier A event fields (generated by `gen_events.py`; each is also a `static_assert` in `offsets_check.cpp`, compiled against `il2cpp.h`).\n')
out_md.append('| Event | id | TypeInfo slot (0 = none; class found by name) | Field | Kind | Offset | dump.cs line |')
out_md.append('|---|---|---|---|---|---|---|')
defs = []
for eid, name in EVENTS + BLOCKED:
    cl, ns, fields = find_class(name)
    cands = [a for a in ti.get(name, []) if a[1] == '%s.%s_TypeInfo' % (ns, name)]
    slot = cands[0][0] if len(cands) == 1 else 0   # informative only; the dylib uses il2cpp_class_from_name
    hname = (ns.replace('.', '_') + '_' if ns else '') + name + '_o'
    rows = []
    for (ln, ty, fn, off) in fields:
        ty = ty.strip()
        if ty in ENUMS4: k, sz = 'ORD_F_I32', 4
        elif ty in KIND: k, sz = KIND[ty]
        else: sys.exit('%s.%s: unknown type %s' % (name, fn, ty))
        rows.append((k, off, fn, ln, sz))
        out_cpp.append('static_assert(offsetof(%s, fields.%s) == 0x%X, "%s.%s");' % (hname, cname(fn), off, name, fn))
        out_cpp.append('static_assert(sizeof(((%s *)0)->fields.%s) == %d, "%s.%s size");' % (hname, cname(fn), sz, name, fn))
        out_md.append('| %s | %d | 0x%X | %s | %s | 0x%X | %d |' % (name, eid, slot, fn, k[6:], off, ln))
    if not fields:
        out_md.append('| %s | %d | 0x%X | (no fields) | | | %d |' % (name, eid, slot, cl))
    blocked = eid >= 200
    fl = FLAGS.get(name, '0') + (' | ORD_EF_BLOCK' if blocked else '')
    out_h.append('static const ord_field ORDF_%s[] = {%s{0, 0}};' % (name, ''.join(
        '{%s, 0x%X}, ' % (k, off) for (k, off, fn, ln, sz) in rows)))
    mo = [off for (k, off, fn, ln, sz) in rows if fn == 'Map' and k == 'ORD_F_I32']
    ow = -1
    for want in OWNER:
        c = [i for i, r in enumerate(rows) if r[2] == want and r[0] in ('ORD_F_GUID', 'ORD_F_ENT')]
        if c: ow = c[0]; break
    defs.append('    {%d, "%s", "%s", %d, ORDF_%s, %s, %d, %d},' % (eid, ns, name, len(rows), name, fl,
                                                              mo[0] if mo else -1, ow))
out_h.append('static const ord_evdef ord_events[] = {')
out_h += defs
out_h.append('};')
out_h.append('#define ORD_NEVENTS ((int)(sizeof ord_events / sizeof ord_events[0]))')
open(os.path.join(HERE, '..', 'ord_events_gen.h'), 'w').write('\n'.join(out_h) + '\n')
open(os.path.join(HERE, 'offsets_check.cpp'), 'w').write('\n'.join(out_cpp) + '\n')
open(os.path.join(HERE, 'OFFSETS_events.md'), 'w').write('\n'.join(out_md) + '\n')
print('events', len(defs))
