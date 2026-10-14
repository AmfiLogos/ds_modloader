//! Unit Builder engine for the DS mod launcher (no UI). See NOTES.md.
//! - `data`: the builder's game data (data/dsb.json, from the wiki builder payload of game build 25846389)
//! - `model`: a build, in the wiki builder's JSON shape
//! - `compute`: the stat sheet, number for number the wiki builder's
//! - `validate`: the builder's legality rules (validate())
//! - `codec`: wiki share links, both ways
//! - `store`: "my units" on disk
//! - `export`: PvP / Foreign Expedition handoff (team link + unit closures as ext/pvp pvp_build.c writes them)
pub mod codec;
pub mod compute;
pub mod data;
pub mod export;
pub mod model;
pub mod validate;

pub use compute::{compute, Sheet};
pub use model::Build;
