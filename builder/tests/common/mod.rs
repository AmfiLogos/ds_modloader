//! Reference checks shared by the comparison tests (export_vs_c.rs, sheet_vs_builder.rs).
//! Each comparison writes one status line to stderr, outside the test capture, so `cargo test` always shows it:
//!   REF RAN  <name>: <what was compared>
//!   REF SKIP <name>: <why>
//! DS_REQUIRE_REFS=1 (release gates): a missing reference, tool or compile error FAILS the test instead of a skip.
//! References and their defaults (see tests/REFERENCES.md):
//!   DS_CM_DIR   Custom Modes checkout (default $HOME/ds_custommodes); C sources taken at DS_CM_REV
//!   DS_CM_REV   commit of DS_CM_DIR to use (default CM_PIN below; "worktree" = the files as they are on disk)
//!   DSB_DIR     the wiki builder, site/builder of the wiki repo (default $HOME/dungeonsettlersdb/site/builder)
#![allow(dead_code)]
use std::io::Write;
use std::path::PathBuf;
use std::process::Command;

/// Custom Modes commit the C export reference is pinned to (bump it when units/ or fe/ change there)
pub const CM_PIN: &str = "fd4a8ce";

pub fn required() -> bool {
    matches!(std::env::var("DS_REQUIRE_REFS").as_deref(), Ok("1") | Ok("true") | Ok("yes"))
}

fn line(s: &str) {
    let mut e = std::io::stderr();
    let _ = writeln!(e, "{s}");
}

pub fn ran(name: &str, what: &str) {
    line(&format!("REF RAN  {name}: {what}"));
}

/// a reference is missing: a failure in required mode, else a visible skip. Returns so the caller can `return`.
pub fn skip(name: &str, why: &str) {
    if required() {
        line(&format!("REF FAIL {name}: {why} (DS_REQUIRE_REFS=1)"));
        panic!("{name}: reference missing in required mode: {why}");
    }
    line(&format!("REF SKIP {name}: {why} (set DS_REQUIRE_REFS=1 to make this a failure)"));
}

fn home() -> PathBuf {
    PathBuf::from(std::env::var("HOME").unwrap_or_default())
}

pub fn cm_dir() -> PathBuf {
    std::env::var("DS_CM_DIR").map(PathBuf::from).unwrap_or_else(|_| home().join("ds_custommodes"))
}

pub fn cm_rev() -> String {
    std::env::var("DS_CM_REV").unwrap_or_else(|_| CM_PIN.into())
}

pub fn dsb_dir() -> PathBuf {
    std::env::var("DSB_DIR").map(PathBuf::from).unwrap_or_else(|_| home().join("dungeonsettlersdb/site/builder"))
}

pub fn node_ok() -> Result<String, String> {
    match Command::new("node").arg("--version").output() {
        Ok(o) if o.status.success() => Ok(String::from_utf8_lossy(&o.stdout).trim().to_string()),
        _ => Err("node not found".into()),
    }
}

/// the commit a directory is at (for the RAN line), or "not a git checkout"
pub fn git_head(dir: &std::path::Path) -> String {
    Command::new("git")
        .arg("-C")
        .arg(dir)
        .args(["log", "-1", "--format=%h %cs"])
        .output()
        .ok()
        .filter(|o| o.status.success())
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .unwrap_or_else(|| "not a git checkout".into())
}
