//! Headless UI check without a window: serves ../ui over http://127.0.0.1:<port> and answers the Unit Builder
//! commands through the real IPC handlers (tests/common). The page gets a small window.__TAURI__ shim that posts
//! to /invoke/<cmd>. Launcher commands (status, play...) are not here: those panels show their error line.
//!   cargo run -p ds-mod-launcher --example ui_bridge -- <port> <scratch root>
#[path = "../tests/common/mod.rs"]
mod common;

use std::io::{BufRead, BufReader, Read, Write};
use std::net::TcpListener;
use std::path::{Path, PathBuf};

const SHIM: &str = r#"window.__TAURI__ = { core: { invoke: function (cmd, args) {
  return fetch('/invoke/' + cmd, { method: 'POST', body: JSON.stringify(args || {}) })
    .then(function (r) { return r.json().then(function (j) { if (r.ok) return j; throw j; }); });
} } };"#;

fn main() {
    let mut a = std::env::args().skip(1);
    let port: u16 = a.next().and_then(|p| p.parse().ok()).unwrap_or(8765);
    let root = PathBuf::from(a.next().unwrap_or_else(|| "../test_env/ui_bridge".into()));
    std::fs::create_dir_all(&root).unwrap();
    let ui = Path::new(env!("CARGO_MANIFEST_DIR")).join("../ui");
    let ipc = common::mock(root);
    let l = TcpListener::bind(("127.0.0.1", port)).expect("port");
    eprintln!("ui_bridge on http://127.0.0.1:{port}/");
    for s in l.incoming() {
        let Ok(mut s) = s else { continue };
        let mut r = BufReader::new(s.try_clone().unwrap());
        let mut line = String::new();
        if r.read_line(&mut line).is_err() { continue }
        let mut parts = line.split_whitespace();
        let (method, path) = (parts.next().unwrap_or("").to_string(), parts.next().unwrap_or("/").to_string());
        let mut len = 0usize;
        loop {
            let mut h = String::new();
            if r.read_line(&mut h).is_err() || h.trim().is_empty() { break }
            if let Some(v) = h.to_ascii_lowercase().strip_prefix("content-length:") { len = v.trim().parse().unwrap_or(0) }
        }
        let mut body = vec![0u8; len];
        let _ = r.read_exact(&mut body);
        let (code, ctype, out): (u16, &str, Vec<u8>) = if method == "POST" && path.starts_with("/invoke/") {
            let cmd = &path["/invoke/".len()..];
            let args: serde_json::Value = serde_json::from_slice(&body).unwrap_or(serde_json::json!({}));
            match ipc.invoke(cmd, args) {
                Ok(v) => (200, "application/json", serde_json::to_vec(&v).unwrap()),
                Err(e) => (500, "application/json", serde_json::to_vec(&e).unwrap()),
            }
        } else {
            let rel = path.split('?').next().unwrap_or("/").trim_start_matches('/');
            let rel = if rel.is_empty() { "index.html" } else { rel };
            if rel == "__bridge.js" {
                (200, "text/javascript", SHIM.as_bytes().to_vec())
            } else if rel.contains("..") {
                (404, "text/plain", b"no".to_vec())
            } else {
                match std::fs::read(ui.join(rel)) {
                    Ok(mut b) => {
                        if rel == "index.html" {
                            let t = String::from_utf8_lossy(&b).replacen("<script src=\"", "<script src=\"__bridge.js\"></script>\n<script src=\"", 1);
                            b = t.into_bytes();
                        }
                        let ct = match rel.rsplit('.').next() {
                            Some("html") => "text/html", Some("js") => "text/javascript", Some("css") => "text/css",
                            Some("png") => "image/png", Some("ttf") => "font/ttf", _ => "application/octet-stream",
                        };
                        (200, ct, b)
                    }
                    Err(_) => (404, "text/plain", b"not found".to_vec()),
                }
            }
        };
        let _ = write!(s, "HTTP/1.1 {code} X\r\nContent-Type: {ctype}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", out.len());
        let _ = s.write_all(&out);
    }
}
