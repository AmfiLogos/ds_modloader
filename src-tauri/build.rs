fn main() {
    // The Unit Builder screen shows game icons from ui/bicons/. They are game art, so they are not in git:
    // builder/gen/extract_icons.py writes them from the builder's own installed game, then manifest.txt last.
    println!("cargo:rerun-if-changed=../ui/bicons/manifest.txt");
    let manifest = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../ui/bicons/manifest.txt");
    // A warning, not a stop: workspace builds and merge checks run without the game. The UI smoke test stops instead.
    if !manifest.is_file() {
        println!("cargo:warning=Unit Builder icons missing (ui/bicons/manifest.txt). They are game art, not in git. \
                  Run: python3 builder/gen/extract_icons.py (needs UnityPy + Pillow; --game <folder> or DS_GAME_DIR \
                  when the game is not in a Steam library). See BUILDING.md, \"Unit Builder icons\".");
    }
    tauri_build::build()
}
