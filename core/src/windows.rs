//! Windows side. Needs a Windows machine to build and test; every function here is a stub on purpose.
//! Plan: research/plan_launcher.md sections 3 and 8 (full copy per build, baselib.dll proxy, unsigned).
use anyhow::{bail, Result};
use std::path::{Path, PathBuf};

/// TODO Windows: full copy of the game folder per build (NTFS has no clone; hard links are NOT safe if
/// Steam patches files in place). Check each file against `GameStock.files` (sha256 list per build).
pub fn build_copy(_steam_game: &Path, _build: &str, _dst: &Path) -> Result<PathBuf> {
    bail!("Windows copy: not built yet (step 5 of the plan, needs a Windows machine)")
}

/// TODO Windows: install `baselib.dll` proxy (original renamed `baselib_orig.dll`, see win/NOTES.md
/// section 4) and the code mods under `mods/<id>/`.
pub fn install_loader(_copy: &Path) -> Result<()> {
    bail!("Windows loader: not built yet (needs the baselib.dll proxy with the plugin API)")
}

/// TODO Windows: start `Dungeon Settlers.exe` from the copy with `steam_appid.txt` next to it.
pub fn launch(_copy: &Path) -> Result<()> {
    bail!("Windows launch: not built yet")
}
