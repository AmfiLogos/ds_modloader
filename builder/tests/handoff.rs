//! The PvP / Foreign Expedition handoff file and the store, end to end (no node needed).
use ds_unit_builder::model::Build;
use ds_unit_builder::validate::validate;
use ds_unit_builder::{codec, export, handoff, store::Store};

fn unit(race: &str, lvl: f64, name: &str) -> Build {
    let mut b = Build::default_build();
    b.race = Some(race.into());
    b.lvl = Some(lvl);
    b.name = Some(name.into());
    b.portrait = None;
    b.eq.insert("Primary".into(), "ITEM_WoodSword".into());
    validate(&mut b);
    b
}

#[test]
fn export_file_carries_the_mod_entries() {
    let builds = vec![unit("AFFECTER_Human", 5.0, "Aldo"), unit("AFFECTER_Elf", 3.0, "Bree")];
    let dir = std::path::PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("handoff");
    let ex = handoff::export_to_dir(&builds, &dir, "test team").expect("export");
    let text = std::fs::read_to_string(&ex.file).unwrap();
    let v: serde_json::Value = serde_json::from_str(&text).expect("export file is JSON");
    assert_eq!(v["format"], "ds-launcher-units-export");
    assert_eq!(v["entries"].as_array().unwrap().len(), 2);
    assert_eq!(v["entries"][0]["manifest"]["source"], "wiki");
    // the link in the file is the team, and the mod builds the same units from it (seeded: same bytes twice)
    let back = codec::decode_link(&ex.link).unwrap();
    assert_eq!(back.units.len(), 2);
    let a = export::entries_from_link(&ex.link, "00112233445566778899aabbccddeeff", Some(7)).unwrap();
    let b = export::entries_from_builds(&builds, "00112233445566778899aabbccddeeff", Some(7)).unwrap();
    assert_eq!(a.iter().map(|e| &e.json).collect::<Vec<_>>(), b.iter().map(|e| &e.json).collect::<Vec<_>>());
    // the entry text in the file is the mod's own compact text, byte for byte
    let raw = export::entries_from_link(&ex.link, "", Some(9)).unwrap();
    let file2 = handoff::export_file_text(&ex.link, &raw);
    assert!(file2.contains(&raw[0].json) && file2.contains(&raw[1].json));
}

#[test]
fn export_refuses_what_the_game_cannot_hold() {
    let mut b = unit("AFFECTER_Human", 5.0, "Cael");
    b.lvl = Some(20.0); // over the game's max level (the builder data caps at 12; validate is skipped here)
    let dir = std::path::PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("handoff");
    let e = handoff::export_to_dir(&[b], &dir, "bad").unwrap_err();
    assert!(e.starts_with("unit 1: level 20"), "{e}");
}

#[test]
fn store_import_and_team_link() {
    let path = std::path::PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join(format!("units_{}.json", ds_unit_builder::store::new_id()));
    let mut s = Store::open(&path).unwrap();
    let link = codec::team_link(&[unit("AFFECTER_Lycan", 4.0, "Dane"), unit("AFFECTER_LizardMan", 2.0, "Esk")]);
    let got = s.import(codec::decode_link(&link).unwrap().units).unwrap();
    let t = s.team_create("Duo", got.iter().map(|u| u.id.clone()).collect()).unwrap();
    s.save().unwrap();
    let s2 = Store::open(&path).unwrap();
    let builds = s2.team_builds(&t.id).unwrap();
    assert_eq!(codec::team_link(&builds), link);
    let _ = std::fs::remove_file(&path);
    let _ = std::fs::remove_file(path.with_extension("json.bak"));
}
