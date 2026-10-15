#!/usr/bin/env python3
"""Write OFFSETS.md: every offset / RVA / metadata slot the orders lane uses, with its dump.cs (or
script.json) source. Field offsets are also static_asserts (offsets_check.cpp, offsets_manual.cpp)."""
import json, os, re, sys
DUMP = os.path.expanduser('~/ds_skill_truth/dump_25846389')
HERE = os.path.dirname(os.path.abspath(__file__))
lines = open(os.path.join(DUMP, 'dump.cs'), encoding='utf-8', errors='replace').read().split('\n')

def class_start(name, kind=r'(?:class|struct)'):
    pat = re.compile(r'^(?:public|internal|private)?\s*(?:sealed |abstract |static )*' + kind + r' ' + re.escape(name) + r'\b.*// TypeDefIndex')
    hits = [i for i, l in enumerate(lines) if pat.match(l)]
    return hits
def field_line(cls, field, off):
    for i in class_start(cls):
        j = i + 1
        while j < len(lines) and not lines[j].startswith('}'):
            m = re.search(r'\s([<>\w]+);\s*// 0x([0-9A-F]+)', lines[j])
            if m and m.group(1) == field:
                return j + 1, int(m.group(2), 16) == off
            j += 1
    return None, False

FIELDS = [  # (class, field, offset, used for)
    ('Interaction', 'InteractionType', 0x0, 'ActionOrder.Execute arg'), ('Interaction', 'ShowUserName', 0x4, ''),
    ('Interaction', 'HideTargetTextInCursor', 0x5, ''), ('Interaction', 'Durability', 0x8, ''),
    ('Interaction', 'TargetText', 0x10, ''), ('Interaction', 'InvalidMessage', 0x18, ''), ('Interaction', 'IsPossible', 0x20, ''),
    ('Interaction', 'UserEntity', 0x28, ''), ('Interaction', 'TargetEntity', 0x30, ''), ('Interaction', 'ItemEntity', 0x38, ''),
    ('Interaction', 'Index', 0x40, ''), ('Interaction', 'InteractionInputType', 0x44, ''),
    ('Interaction', 'InteractionUIInput', 0x48, ''), ('Interaction', 'TaskType', 0xD0, ''),
    ('UIInput', 'UIInputType', 0x0, 'Interaction+0x48'), ('UIInput', 'TradeCommit', 0x8, 'not carried'),
    ('UIInput', 'ParamIndex1', 0x10, ''), ('UIInput', 'ParamIndex2', 0x14, ''), ('UIInput', 'ParamIndex3', 0x18, ''),
    ('UIInput', 'ParamString1', 0x20, ''), ('UIInput', 'ParamString2', 0x28, ''), ('UIInput', 'ParamGuid1', 0x30, ''),
    ('UIInput', 'ParamGuid2', 0x40, ''), ('UIInput', 'ParamGuid3', 0x50, 'Nullable<Guid>'),
    ('UIInput', 'ParamGuidList', 0x68, 'not carried'), ('UIInput', 'ParamBool1', 0x70, ''), ('UIInput', 'ParamBool2', 0x71, ''),
    ('UIInput', 'ParamBool3', 0x72, ''),
    ('TradeCommitRequest', '<Map>k__BackingField', 0x10, 'trade_get/new'), ('TradeCommitRequest', '<SessionId>k__BackingField', 0x14, 'rewritten on replay'),
    ('TradeCommitRequest', '<Deliveries>k__BackingField', 0x28, 'ReadOnlyCollection'),
    ('TradeCommitDelivery', '<SlotId>k__BackingField', 0x0, 'array stride 0x28'), ('TradeCommitDelivery', '<Destination>k__BackingField', 0x10, ''),
    ('TradeCommitDelivery', '<TargetOwnerId>k__BackingField', 0x14, ''), ('TradeCommitDelivery', '<Amount>k__BackingField', 0x24, ''),
    ('TradeHelper', '_sessionId', 0x44, 'TryCommit hook'),
    ('SelectDataContainer', '_selectGroup', 0x10, 'selection capture / swap'), ('SelectDataContainer', '_selectedEntities', 0x18, ''),
    ('MainFlow', '_eventQueue', 0x48, 'replay dispatch'), ('MainFlow', '_eventHandler', 0x50, 'navigation root'),
    ('MainFlow', '_inputDispatcher', 0x68, 'observer buffer'), ('MainFlow', '_campaignData', 0xC8, 'item container fallback'),
    ('MainFlow', '_inMapDatas', 0x128, 'Guid -> entity'),
    ('EntityDataContainer', '_itemContainers', 0x28, 'item fallback'),
    ('EventQueue', '_syncEventHandlerMap', 0x28, 'sync handler delegate'), ('EventQueue', '_asyncEventHandlerMap', 0x30, 'async: AddEvent'),
    ('EventHandler', '<ItemEventHandler>k__BackingField', 0x10, ''), ('EventHandler', '<PlayerControlEventHandler>k__BackingField', 0x38, ''),
    ('PlayerControlEventHandler', '_entityComponent', 0x18, 'GetComponentOf<SkillComponent>'),
    ('PlayerControlEventHandler', '_modeProcessor', 0x20, 'GetProcessor'), ('PlayerControlEventHandler', '_selectReader', 0x48, 'selection container'),
    ('ItemEventHandler', '_inMapDatas', 0x18, 'raycast arg'), ('ItemEventHandler', '_objectEntityGuidReader', 0x88, 'raycast arg'),
    ('DefaultMode', '_command', 0x18, ''), ('DefaultCommand', '_flow', 0x20, ''),
    ('CommandFlow', '_runner', 0x10, ''), ('CommandFlow', '_move', 0x18, 'MoveOrder.All / TryMove this'), ('CommandFlow', '_waypoint', 0x20, 'WaypointOrder this'),
    ('InteractionRunner', '_check', 0x10, 'RunCheck'), ('InteractionRunner', '_action', 0x18, 'ActionOrder this'), ('InteractionRunner', '_move', 0x20, 'MoveOrder.Near this'),
    ('InputDispatcher', '_inputBinding', 0x28, ''), ('InputBinding', '_buffer', 0x38, 'ObserverAggregator'),
    ('UnitEntity', '<Guid>k__BackingField', 0x24, 'entity -> Guid'), ('BuildingEntity', '<Guid>k__BackingField', 0x10, ''),
    ('ItemEntity', '<Guid>k__BackingField', 0x10, ''), ('ThingEntity', '<Guid>k__BackingField', 0x10, ''),
    ('FarmEntity', '<Guid>k__BackingField', 0x10, ''), ('BlueprintEntity', '<Guid>k__BackingField', 0x10, ''),
    ('NullEntity', '<Guid>k__BackingField', 0x10, ''),
    ('SkillComponent', '_owner', 0x10, 'auto-skill unit'), ('Skill_Base', '<Key>k__BackingField', 0x10, 'skill key'),
    ('GridData', '<Position>k__BackingField', 0x10, 'grid selector'), ('GridData', '<Flag>k__BackingField', 0x18, ''),
    ('InputObserver', 'InputType', 0x10, 'tutorial observer'),
    ('CampaignDataContainer', 'EntityDataContainer', 0x20, ''),
]
RVAS = [l for l in open(os.path.join(HERE, '..', 'ord_game.c')).read().split('\n') if re.match(r'#define (VA|TI|MI)_', l)]

out = ['# Offsets, RVAs and metadata slots used by the orders lane (build 25846389)', '',
       'Generated by `gen_offsets_md.py`. Field offsets are also `static_assert`s compiled against `il2cpp.h` by `run_tests.sh` '
       '(`offsets_manual.cpp`, `offsets_check.cpp`). Tier A event fields: `OFFSETS_events.md`.', '',
       '## Fields (outside the event table)', '', '| Class | Field | Offset | dump.cs line | Used for |', '|---|---|---|---|---|']
bad = 0
for cls, f, off, why in FIELDS:
    ln, ok = field_line(cls, f, off)
    if not ok: bad += 1
    out.append('| %s | %s | 0x%X | %s | %s |' % (cls, f.replace('<', '&lt;').replace('>', '&gt;'), off, ln if ok else 'MISMATCH %s' % ln, why))
out += ['', '## RVAs and metadata slots (ord_game.c)', '', '| Name | Value | dump.cs line / source | Comment |', '|---|---|---|---|']
for l in RVAS:
    m = re.match(r'#define (\w+)\s+(0x[0-9A-Fa-f]+)\s*(?:/\*\s*(.*?)\s*\*/)?', l)
    if not m: continue
    name, val, com = m.group(1), int(m.group(2), 16), m.group(3) or ''
    src = ''
    if name.startswith('VA_'):
        pat = '// RVA: 0x%X ' % val
        hit = [i + 1 for i, x in enumerate(lines) if pat in x]
        src = str(hit[0]) if hit else ('il2cpp export (nm GameAssembly)' if name.startswith('VA_IL_')
                                        else 'script.json ScriptMethod (shared generic body)')
    else:
        src = 'script.json metadata slot'
    out.append('| %s | 0x%X | %s | %s |' % (name, val, src, com))
open(os.path.join(HERE, 'OFFSETS.md'), 'w').write('\n'.join(out) + '\n')
print('OFFSETS.md: %d fields (%d mismatches), %d RVAs/slots' % (len(FIELDS), bad, len(RVAS)))
sys.exit(1 if bad else 0)
