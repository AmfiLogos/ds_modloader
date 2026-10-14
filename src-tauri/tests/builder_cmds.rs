//! The Unit Builder commands through the launcher's IPC (mock runtime, same handlers + state as main.rs):
//! argument names as the UI sends them, results as the UI reads them.
mod common;
use serde_json::{json, Value};

fn scratch(name: &str) -> std::path::PathBuf {
    let d = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../test_env").join(name);
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

#[test]
fn builder_commands_round_trip() {
    let root = scratch("builder_cmds");
    let ipc = common::mock(root.clone());

    let cat = ipc.invoke("builder_catalog", json!({})).unwrap();
    assert!(cat["dsb"]["races"].as_array().unwrap().len() >= 4);
    assert_eq!(cat["wikiUrl"], "https://dungeonsettlers.wiki/builder");

    // pick a race, change the level: the sheet follows
    let mut b = ipc.invoke("builder_new_unit", json!({ "race": "AFFECTER_Elf" })).unwrap();
    assert_eq!(b["race"], "AFFECTER_Elf");
    let live5 = ipc.invoke("builder_compute", json!({ "build": b })).unwrap();
    b["lvl"] = json!(10);
    let b = ipc.invoke("builder_validate", json!({ "build": b })).unwrap();
    assert_eq!(b["lvl"], json!(10.0));
    let live10 = ipc.invoke("builder_compute", json!({ "build": b })).unwrap();
    let hp = |l: &Value| l["sheet"]["st"]["MaxHealthTotal"].as_f64().unwrap();
    assert!(hp(&live10) > hp(&live5), "level 10 has more HP than level 5");
    assert!(live10["inscSlots"].as_f64().unwrap() > live5["inscSlots"].as_f64().unwrap());

    // learn the first skill of the first active tree that fits
    let tree = live10["activeTrees"][0].as_str().unwrap().to_string();
    let skill = cat["dsb"]["trees"][&tree]["rows"][0].as_array().unwrap().iter().find_map(|x| x.as_str()).unwrap().to_string();
    let mut b2 = b.clone();
    b2["learned"] = json!([skill]);
    let b2 = ipc.invoke("builder_validate", json!({ "build": b2 })).unwrap();
    assert_eq!(b2["learned"], json!([skill]));

    // gear with a roll, through the slot command
    let b3 = ipc.invoke("builder_set_item", json!({ "build": b2, "slot": "Accessory1", "item": "ITEM_SilverNecklace" })).unwrap();
    assert_eq!(b3["eq"]["Accessory1"], "ITEM_SilverNecklace");
    let mut b3 = b3;
    b3["tags"]["Accessory1"] = json!([{ "i": 3, "v": 7 }]);
    let b3 = ipc.invoke("builder_validate", json!({ "build": b3 })).unwrap();

    // copy / paste: the link gives the same unit back
    let links = ipc.invoke("builder_encode_link", json!({ "builds": [b3] })).unwrap();
    let solo = links["solo"].as_str().unwrap();
    assert!(solo.contains("#b=~"));
    let dec = ipc.invoke("builder_decode_link", json!({ "text": solo })).unwrap();
    let u = &dec["units"][0];
    for k in ["race", "bg", "lvl", "tal", "learned", "eq", "tags", "name", "portrait"] {
        assert_eq!(u[k], b3[k], "field {k} survives the link");
    }
    assert!(ipc.invoke("builder_decode_link", json!({ "text": "not a link" })).is_err());
    assert!(ipc.invoke("builder_encode_link", json!({ "builds": [] })).is_err());

    // my units: save, list, rename, duplicate, team, export, delete
    let saved = ipc.invoke("units_save", json!({ "id": null, "build": b3 })).unwrap();
    let id = saved["id"].as_str().unwrap().to_string();
    assert!(root.join("builder/units.json").exists());
    let again = ipc.invoke("units_save", json!({ "id": id, "build": b3 })).unwrap();
    assert_eq!(again["id"], saved["id"], "saving again updates the same unit");
    let renamed = ipc.invoke("units_rename", json!({ "id": id, "name": "Smoke" })).unwrap();
    assert_eq!(renamed["name"], "Smoke");
    let dup = ipc.invoke("units_duplicate", json!({ "id": id })).unwrap();
    let list = ipc.invoke("units_list", json!({})).unwrap();
    assert_eq!(list[0].as_array().unwrap().len(), 2);
    let team = ipc.invoke("teams_create", json!({ "name": "Pair", "unitIds": [id, dup["id"]] })).unwrap();
    assert_eq!(team["unit_ids"].as_array().unwrap().len(), 2);
    let ex = ipc.invoke("units_export", json!({ "unitIds": [id, dup["id"]], "name": "Pair" })).unwrap();
    assert!(ex["link"].as_str().unwrap().contains("?team="));
    assert!(std::path::Path::new(ex["file"].as_str().unwrap()).exists());
    let back = ipc.invoke("builder_decode_link", json!({ "text": ex["link"] })).unwrap();
    assert_eq!(back["units"].as_array().unwrap().len(), 2);
    let imported = ipc.invoke("units_import_link", json!({ "text": solo })).unwrap();
    assert_eq!(imported.as_array().unwrap().len(), 1);
    ipc.invoke("teams_delete", json!({ "id": team["id"] })).unwrap();
    ipc.invoke("units_delete", json!({ "id": dup["id"] })).unwrap();
    let list = ipc.invoke("units_list", json!({})).unwrap();
    assert_eq!(list[0].as_array().unwrap().len(), 2);
    assert_eq!(list[1].as_array().unwrap().len(), 0);
    assert!(ipc.invoke("units_export", json!({ "unitIds": ["nope"] })).is_err());
}
