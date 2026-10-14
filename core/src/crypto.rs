//! ed25519 signature of index.json, sha256 of every file.
//! Signature file `index.json.sig` = JSON {"key_id": "<16 hex>", "sig": "<base64 64 bytes>"} over the exact
//! bytes of index.json. key_id = first 8 bytes of sha256(public key), so a launcher can carry two keys
//! (current + rotation) from day one.
use anyhow::{anyhow, bail, Context, Result};
use base64::{engine::general_purpose::STANDARD as B64, Engine};
use ed25519_dalek::{Signature, Signer, SigningKey, Verifier, VerifyingKey};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::io::Read;
use std::path::Path;

use crate::model::Index;

pub fn sha256_hex(data: &[u8]) -> String { hex::encode(Sha256::digest(data)) }

pub fn sha256_file(p: &Path) -> Result<String> {
    let mut f = std::fs::File::open(p).with_context(|| format!("open {}", p.display()))?;
    let mut h = Sha256::new();
    let mut buf = vec![0u8; 1 << 20];
    loop {
        let n = f.read(&mut buf)?;
        if n == 0 { break; }
        h.update(&buf[..n]);
    }
    Ok(hex::encode(h.finalize()))
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SigFile {
    pub key_id: String,
    pub sig: String,
}

pub fn parse_public_key(b64: &str) -> Result<VerifyingKey> {
    let raw = B64.decode(b64.trim()).context("public key is not base64")?;
    let arr: [u8; 32] = raw.as_slice().try_into().map_err(|_| anyhow!("public key must be 32 bytes"))?;
    VerifyingKey::from_bytes(&arr).map_err(|e| anyhow!("bad public key: {e}"))
}

pub fn parse_secret_key(b64: &str) -> Result<SigningKey> {
    let raw = B64.decode(b64.trim()).context("secret key is not base64")?;
    let arr: [u8; 32] = raw.as_slice().try_into().map_err(|_| anyhow!("secret key must be 32 bytes"))?;
    Ok(SigningKey::from_bytes(&arr))
}

pub fn key_id(pk: &VerifyingKey) -> String { hex::encode(&Sha256::digest(pk.as_bytes())[..8]) }

/// New key pair from the OS random source: (secret base64, public base64).
pub fn keygen() -> Result<(String, String)> {
    let mut seed = [0u8; 32];
    std::fs::File::open("/dev/urandom")?.read_exact(&mut seed)?;
    let sk = SigningKey::from_bytes(&seed);
    Ok((B64.encode(seed), B64.encode(sk.verifying_key().as_bytes())))
}

pub fn sign(secret_b64: &str, data: &[u8]) -> Result<SigFile> {
    let sk = parse_secret_key(secret_b64)?;
    Ok(SigFile { key_id: key_id(&sk.verifying_key()), sig: B64.encode(sk.sign(data).to_bytes()) })
}

/// Checks the signature with one of the trusted keys, then parses the index.
pub fn verify_index(index_bytes: &[u8], sig_bytes: &[u8], trusted: &[&str]) -> Result<Index> {
    let sf: SigFile = serde_json::from_slice(sig_bytes).context("index.json.sig is not valid")?;
    let raw = B64.decode(sf.sig.trim()).context("signature is not base64")?;
    let sig = Signature::from_slice(&raw).map_err(|_| anyhow!("signature must be 64 bytes"))?;
    let mut known = false;
    for t in trusted {
        let pk = parse_public_key(t)?;
        if key_id(&pk) != sf.key_id { continue; }
        known = true;
        if pk.verify(index_bytes, &sig).is_ok() {
            return serde_json::from_slice(index_bytes).context("index.json does not parse");
        }
    }
    if known { bail!("index.json signature is NOT valid (file changed after signing)") }
    bail!("index.json is signed with an unknown key {}", sf.key_id)
}

/// Replay guard: an older index than the last one seen is refused.
pub fn check_seq(index: &Index, last_seen: u64) -> Result<()> {
    if index.seq < last_seen {
        bail!("index.json is older than the last one seen (seq {} < {}): refused", index.seq, last_seen);
    }
    Ok(())
}

/// A downloaded file must have the exact size and sha256 the signed index lists.
pub fn check_file(p: &Path, sha256: &str, size: u64) -> Result<()> {
    let len = std::fs::metadata(p).with_context(|| format!("missing {}", p.display()))?.len();
    if len != size { bail!("{}: size {} but the index says {}", p.display(), len, size) }
    let got = sha256_file(p)?;
    if !got.eq_ignore_ascii_case(sha256) { bail!("{}: sha256 {} but the index says {}", p.display(), got, sha256) }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn index_bytes(seq: u64) -> Vec<u8> {
        format!(r#"{{"schema":1,"seq":{seq},"games":{{}},"mods":[]}}"#).into_bytes()
    }

    #[test]
    fn good_signature_passes() {
        let (sk, pk) = keygen().unwrap();
        let data = index_bytes(3);
        let sig = serde_json::to_vec(&sign(&sk, &data).unwrap()).unwrap();
        let idx = verify_index(&data, &sig, &[&pk]).unwrap();
        assert_eq!(idx.seq, 3);
    }

    #[test]
    fn changed_index_fails() {
        let (sk, pk) = keygen().unwrap();
        let sig = serde_json::to_vec(&sign(&sk, &index_bytes(3)).unwrap()).unwrap();
        let err = verify_index(&index_bytes(4), &sig, &[&pk]).unwrap_err().to_string();
        assert!(err.contains("NOT valid"), "{err}");
    }

    #[test]
    fn unknown_key_fails() {
        let (sk, _) = keygen().unwrap();
        let (_, other_pk) = keygen().unwrap();
        let data = index_bytes(1);
        let sig = serde_json::to_vec(&sign(&sk, &data).unwrap()).unwrap();
        let err = verify_index(&data, &sig, &[&other_pk]).unwrap_err().to_string();
        assert!(err.contains("unknown key"), "{err}");
    }

    #[test]
    fn rotation_key_is_accepted() {
        let (_, pk_old) = keygen().unwrap();
        let (sk_new, pk_new) = keygen().unwrap();
        let data = index_bytes(9);
        let sig = serde_json::to_vec(&sign(&sk_new, &data).unwrap()).unwrap();
        assert!(verify_index(&data, &sig, &[&pk_old, &pk_new]).is_ok());
    }

    #[test]
    fn replayed_old_index_is_refused() {
        let idx: Index = serde_json::from_slice(&index_bytes(4)).unwrap();
        assert!(check_seq(&idx, 4).is_ok());
        assert!(check_seq(&idx, 3).is_ok());
        assert!(check_seq(&idx, 5).is_err());
    }

    #[test]
    fn checksum_and_size_are_checked() {
        let d = std::env::temp_dir().join(format!("dsml_ck_{}", std::process::id()));
        std::fs::create_dir_all(&d).unwrap();
        let p = d.join("f.bin");
        std::fs::write(&p, b"hello").unwrap();
        let good = sha256_hex(b"hello");
        assert!(check_file(&p, &good, 5).is_ok());
        assert!(check_file(&p, &good, 6).unwrap_err().to_string().contains("size"));
        assert!(check_file(&p, &sha256_hex(b"hellO"), 5).unwrap_err().to_string().contains("sha256"));
    }
}
