//! The PvP / Foreign Expedition handoff: the team link (what the mod's "Paste Wiki Link" box takes today) and an
//! export file with the unit closures the mod builds from that link (src/export.rs, byte-identical to
//! ext/pvp pvp_build.c). File format: NOTES.md, "Export for PvP / Foreign Expedition".
use crate::codec;
use crate::export;
use crate::model::Build;
use serde::Serialize;

#[derive(Debug, Serialize)]
pub struct Exported {
    /// the team link: paste it in the PvP "Bring Units" window (Paste Wiki Link)
    pub link: String,
    /// the export file written (the closure entries, the same bytes the mod builds from the link)
    pub file: String,
    pub names: Vec<String>,
    pub notes: Vec<String>,
}

/// check that the mod can build every unit (same rules and refusal texts as pvp_build.c), write
/// `<dir>/<name>-<ms>.json`, return the team link
pub fn export_to_dir(builds: &[Build], dir: &std::path::Path, name: &str) -> Result<Exported, String> {
    if builds.is_empty() || builds.len() > codec::MAX_TEAM {
        return Err(format!("Export 1 to {} units.", codec::MAX_TEAM));
    }
    let code = codec::encode_team(builds);
    let link = format!("{}?team={code}", codec::WIKI_BUILDER_URL);
    let entries = export::entries_from_link(&link, "", None)?;
    let text = export_file_text(&link, &entries);
    std::fs::create_dir_all(dir).map_err(|e| format!("Could not write the export: {e}"))?;
    let safe: String = name.chars().map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' { c } else { '-' }).collect();
    let path = dir.join(format!("{}-{}.json", if safe.is_empty() { "units" } else { &safe }, crate::store::now_ms()));
    std::fs::write(&path, text).map_err(|e| format!("Could not write the export: {e}"))?;
    Ok(Exported {
        link,
        file: path.display().to_string(),
        names: entries.iter().map(|e| e.name_key.clone()).collect(),
        notes: entries.iter().flat_map(|e| e.notes.clone()).collect(),
    })
}

/// the export file: the entries are written as the mod's own compact text (byte for byte; the closure hash is
/// computed over it), wrapped with the link and the data's game version
pub fn export_file_text(link: &str, entries: &[export::Entry]) -> String {
    let d = crate::data::dsb();
    let head = serde_json::json!({
        "format": "ds-launcher-units-export",
        "version": 1,
        "game_build": d.game_build(),
        "game_version": d.game_version(),
        "link": link,
    })
    .to_string();
    let body: Vec<&str> = entries.iter().map(|e| e.json.as_str()).collect();
    format!("{},\"entries\":[{}]}}", &head[..head.len() - 1], body.join(","))
}
