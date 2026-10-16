fn main() {
    // The Unit Builder screen shows game icons from ui/bicons/. They are game art, so they are not in git:
    // builder/gen/extract_icons.py writes them from the builder's own installed game, then manifest.txt last.
    println!("cargo:rerun-if-changed=../ui/bicons/manifest.txt");
    let manifest = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../ui/bicons/manifest.txt");
    if !manifest.is_file() {
        eprintln!(
            "\nerror: the Unit Builder icons are missing (ui/bicons/manifest.txt not found).\n\
             They are game art and are not in the repo. Extract them from your installed Dungeon Settlers:\n\
             \n    python3 builder/gen/extract_icons.py            (needs: pip install UnityPy Pillow)\n\
             \nIf the game is not in a Steam library on this computer, add --game <game folder> or set DS_GAME_DIR.\n\
             See BUILDING.md, section \"Unit Builder icons\".\n"
        );
        std::process::exit(1);
    }
    tauri_build::build()
}
