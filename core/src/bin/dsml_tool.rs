//! Publisher tool (owner's Mac only).
//!   dsml-tool keygen <secret-out> <public-out>    new ed25519 key pair (base64 files)
//!   dsml-tool sign <index.json> <secret-key>       writes <index.json>.sig
//!   dsml-tool verify <index.json> <public-key>...  checks <index.json>.sig
//!   dsml-tool file <path> <url>                    prints a FileEntry (name, sha256, size, url)
//!   dsml-tool patches <stock> <modded> <path-in-app> <arm64|file>   prints the byte patches (JSON)
use anyhow::{bail, Context, Result};
use dsml_core::crypto;

fn main() -> Result<()> {
    let a: Vec<String> = std::env::args().skip(1).collect();
    match a.first().map(String::as_str) {
        Some("keygen") if a.len() == 3 => {
            for p in &a[1..] { if std::path::Path::new(p).exists() { bail!("{p} exists: not overwritten") } }
            let (sk, pk) = crypto::keygen()?;
            std::fs::write(&a[1], format!("{sk}\n"))?;
            std::fs::write(&a[2], format!("{pk}\n"))?;
            println!("key id {}", crypto::key_id(&crypto::parse_public_key(&pk)?));
        }
        Some("sign") if a.len() == 3 => {
            let data = std::fs::read(&a[1])?;
            serde_json::from_slice::<dsml_core::model::Index>(&data).context("index.json does not parse")?;
            let sk = std::fs::read_to_string(&a[2])?;
            let sig = crypto::sign(&sk, &data)?;
            std::fs::write(format!("{}.sig", a[1]), serde_json::to_vec(&sig)?)?;
            println!("signed with key {}", sig.key_id);
        }
        Some("verify") if a.len() >= 3 => {
            let keys: Vec<String> = a[2..].iter().map(std::fs::read_to_string).collect::<Result<_, _>>()?;
            let refs: Vec<&str> = keys.iter().map(String::as_str).collect();
            let idx = crypto::verify_index(&std::fs::read(&a[1])?, &std::fs::read(format!("{}.sig", a[1]))?, &refs)?;
            println!("ok: seq {}, {} mod versions", idx.seq, idx.mods.len());
        }
        Some("file") if a.len() == 3 => {
            let p = std::path::Path::new(&a[1]);
            let e = dsml_core::model::FileEntry {
                name: p.file_name().unwrap().to_string_lossy().into_owned(),
                sha256: crypto::sha256_file(p)?,
                size: std::fs::metadata(p)?.len(),
                url: a[2].clone(),
                dest: None,
            };
            println!("{}", serde_json::to_string_pretty(&e)?);
        }
        Some("patches") if a.len() == 5 => {
            let ps = dsml_core::copy_mac::diff_patches(&std::fs::read(&a[1])?, &std::fs::read(&a[2])?, &a[3], &a[4])?;
            println!("{}", serde_json::to_string_pretty(&ps)?);
        }
        _ => bail!("usage: dsml-tool keygen|sign|verify|file ... (see the source header)"),
    }
    Ok(())
}
