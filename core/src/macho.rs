//! Just enough Mach-O reading: is it Mach-O, where is the arm64 slice, what macOS does it need.
use anyhow::{bail, Result};

const FAT_MAGIC: u32 = 0xCAFEBABE;
const MH_MAGIC_64: u32 = 0xFEEDFACF;
const CPU_ARM64: i32 = 0x0100000C;
const LC_BUILD_VERSION: u32 = 0x32;
const LC_VERSION_MIN_MACOSX: u32 = 0x24;

fn be32(b: &[u8], o: usize) -> u32 { u32::from_be_bytes(b[o..o + 4].try_into().unwrap()) }
fn le32(b: &[u8], o: usize) -> u32 { u32::from_le_bytes(b[o..o + 4].try_into().unwrap()) }

pub fn is_macho(b: &[u8]) -> bool {
    b.len() >= 4 && (be32(b, 0) == FAT_MAGIC || le32(b, 0) == MH_MAGIC_64)
}

/// (offset, size) of the arm64 slice. A thin arm64 file is its own slice.
pub fn arm64_slice(b: &[u8]) -> Result<(usize, usize)> {
    if b.len() < 8 { bail!("too short for Mach-O") }
    if be32(b, 0) == FAT_MAGIC {
        let n = be32(b, 4) as usize;
        for i in 0..n {
            let o = 8 + 20 * i;
            if o + 20 > b.len() { break; }
            if be32(b, o) as i32 == CPU_ARM64 {
                return Ok((be32(b, o + 8) as usize, be32(b, o + 12) as usize));
            }
        }
        bail!("fat file has no arm64 slice")
    }
    if le32(b, 0) == MH_MAGIC_64 && le32(b, 4) as i32 == CPU_ARM64 { return Ok((0, b.len())) }
    bail!("not an arm64 Mach-O file")
}

/// Minimum macOS of the arm64 slice as (major, minor), from LC_BUILD_VERSION or LC_VERSION_MIN_MACOSX.
pub fn min_macos(b: &[u8]) -> Result<(u32, u32)> {
    let (off, size) = arm64_slice(b)?;
    let s = &b[off..off + size];
    let ncmds = le32(s, 16) as usize;
    let mut o = 32;
    for _ in 0..ncmds {
        if o + 8 > s.len() { break; }
        let (cmd, sz) = (le32(s, o), le32(s, o + 4) as usize);
        if cmd == LC_BUILD_VERSION || cmd == LC_VERSION_MIN_MACOSX {
            let v = le32(s, o + if cmd == LC_BUILD_VERSION { 12 } else { 8 });
            return Ok((v >> 16, (v >> 8) & 0xff));
        }
        if sz == 0 { break; }
        o += sz;
    }
    bail!("no minimum macOS version in the file")
}

/// The running macOS as (major, minor).
pub fn running_macos() -> (u32, u32) {
    let out = std::process::Command::new("/usr/bin/sw_vers").arg("-productVersion").output();
    let s = out.map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string()).unwrap_or_default();
    let mut it = s.split('.').map(|x| x.parse::<u32>().unwrap_or(0));
    (it.next().unwrap_or(0), it.next().unwrap_or(0))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn thin(minos: u32) -> Vec<u8> {
        let mut b = vec![0u8; 32 + 24];
        b[0..4].copy_from_slice(&MH_MAGIC_64.to_le_bytes());
        b[4..8].copy_from_slice(&(CPU_ARM64 as u32).to_le_bytes());
        b[16..20].copy_from_slice(&1u32.to_le_bytes());
        b[32..36].copy_from_slice(&LC_BUILD_VERSION.to_le_bytes());
        b[36..40].copy_from_slice(&24u32.to_le_bytes());
        b[44..48].copy_from_slice(&minos.to_le_bytes());
        b
    }

    #[test]
    fn reads_minos_of_thin_and_fat() {
        let t = thin(12 << 16);
        assert!(is_macho(&t));
        assert_eq!(min_macos(&t).unwrap(), (12, 0));
        // fat: header + one arm64 entry at offset 64
        let mut f = vec![0u8; 64];
        f[0..4].copy_from_slice(&FAT_MAGIC.to_be_bytes());
        f[4..8].copy_from_slice(&1u32.to_be_bytes());
        f[8..12].copy_from_slice(&(CPU_ARM64 as u32).to_be_bytes());
        f[16..20].copy_from_slice(&64u32.to_be_bytes());
        let t2 = thin((27 << 16) | (2 << 8));
        f[20..24].copy_from_slice(&(t2.len() as u32).to_be_bytes());
        f.extend_from_slice(&t2);
        assert_eq!(arm64_slice(&f).unwrap(), (64, t2.len()));
        assert_eq!(min_macos(&f).unwrap(), (27, 2));
        assert!(!is_macho(b"plain text file"));
    }
}
