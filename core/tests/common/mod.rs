//! Test helpers: a local file server standing in for GitHub Releases, and index signing.
#![allow(dead_code)]
use std::io::{BufRead, BufReader, Write};
use std::net::TcpListener;
use std::path::{Path, PathBuf};

/// Serves files of `dir` at http://127.0.0.1:<port>/<name>. Returns the base url.
pub fn serve(dir: PathBuf) -> String {
    let l = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = l.local_addr().unwrap().port();
    std::thread::spawn(move || {
        for s in l.incoming().flatten() {
            let dir = dir.clone();
            std::thread::spawn(move || {
                let mut r = BufReader::new(s.try_clone().unwrap());
                let mut line = String::new();
                if r.read_line(&mut line).is_err() { return; }
                loop { let mut h = String::new(); if r.read_line(&mut h).unwrap_or(0) <= 2 { break; } }
                let path = line.split_whitespace().nth(1).unwrap_or("/").trim_start_matches('/').to_string();
                let mut s = s;
                match (!path.contains("..")).then(|| std::fs::read(dir.join(&path))).and_then(Result::ok) {
                    Some(b) => {
                        let _ = write!(s, "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", b.len());
                        let _ = s.write_all(&b);
                    }
                    None => { let _ = write!(s, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); }
                }
            });
        }
    });
    format!("http://127.0.0.1:{port}")
}

pub fn launcher_dir() -> PathBuf { PathBuf::from(env!("CARGO_MANIFEST_DIR")).parent().unwrap().to_path_buf() }

/// Writes index.json + index.json.sig into `dir`, signed with the DEV key.
pub fn publish_index(dir: &Path, index: &serde_json::Value) {
    let data = serde_json::to_vec_pretty(index).unwrap();
    let sk = std::fs::read_to_string(launcher_dir().join("dev_keys/dev.key")).unwrap();
    let sig = dsml_core::crypto::sign(&sk, &data).unwrap();
    std::fs::write(dir.join("index.json"), &data).unwrap();
    std::fs::write(dir.join("index.json.sig"), serde_json::to_vec(&sig).unwrap()).unwrap();
}

pub fn dev_pub() -> String { std::fs::read_to_string(launcher_dir().join("dev_keys/dev.pub")).unwrap() }

pub fn file_entry(p: &Path, base_url: &str) -> serde_json::Value {
    let name = p.file_name().unwrap().to_string_lossy().into_owned();
    serde_json::json!({ "name": name, "sha256": dsml_core::crypto::sha256_file(p).unwrap(),
                        "size": std::fs::metadata(p).unwrap().len(), "url": format!("{base_url}/{name}") })
}

pub fn fresh_dir(label: &str) -> PathBuf {
    static N: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
    let n = N.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
    let d = launcher_dir().join("test_env").join(format!("{label}-{}-{}-{n}", dsml_core::now_secs(), std::process::id()));
    std::fs::create_dir_all(&d).unwrap();
    d
}
