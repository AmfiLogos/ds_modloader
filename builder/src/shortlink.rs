//! Short wiki links: `https://dungeonsettlers.wiki[/<lang>]/t/<id>` (a team) and `.../build/<id>` (one unit). The
//! wiki answers them with a 302 to the long builder link (or an HTML page with a meta refresh). One GET to
//! dungeonsettlers.wiki only, redirects NOT followed, 5 s timeout; the target must be a dungeonsettlers.wiki
//! /builder link, then `codec::decode_link` (pure) decodes it.
use crate::codec::{self, Decoded};
use std::time::Duration;

pub const WIKI_HOST: &str = "dungeonsettlers.wiki";
pub const MSG_OFFLINE: &str = "This short link needs the internet. Or open it on the wiki and copy the long link.";
pub const MSG_GONE: &str = "This short link does not exist (removed?)";

/// the https URL to ask, when `text` is a short wiki link; None otherwise
pub fn short_url(text: &str) -> Option<String> {
    let t = text.trim();
    let rest = t.strip_prefix("https://").or_else(|| t.strip_prefix("http://")).unwrap_or(t);
    let rest = rest.strip_prefix("www.").unwrap_or(rest);
    let path = rest.strip_prefix(WIKI_HOST)?;
    let path = path.split(['?', '#']).next().unwrap_or("");
    let segs: Vec<&str> = path.trim_matches('/').split('/').collect();
    let (kind, id) = match segs.as_slice() {
        [k, id] => (*k, *id),
        [lang, k, id] if is_lang(lang) => (*k, *id),
        _ => return None,
    };
    if !(kind == "t" || kind == "build") || id.is_empty() || id.len() > 64 {
        return None;
    }
    if !id.chars().all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_') {
        return None;
    }
    Some(format!("https://{WIKI_HOST}/{}", segs.join("/")))
}
fn is_lang(s: &str) -> bool {
    (2..=7).contains(&s.len()) && s.chars().all(|c| c.is_ascii_alphabetic() || c == '-')
}

/// the URL of `<meta http-equiv="refresh" content="0; url=...">` in an HTML page
pub fn meta_refresh(html: &str) -> Option<String> {
    let low = html.to_ascii_lowercase();
    let mut from = 0;
    while let Some(p) = low[from..].find("<meta") {
        let start = from + p;
        let end = low[start..].find('>').map(|e| start + e).unwrap_or(low.len());
        let tag = &low[start..end];
        from = end;
        if !tag.contains("http-equiv") || !tag.contains("refresh") {
            continue;
        }
        let c = tag.find("content")?;
        let after = &tag[c + 7..];
        let q = after.find(['"', '\''])?;
        let quote = after.as_bytes()[q] as char;
        let vstart = start + c + 7 + q + 1;
        let vlen = html[vstart..].find(quote)?;
        let content = &html[vstart..vstart + vlen];
        let lc = content.to_ascii_lowercase();
        let u = lc.find("url=")?;
        let url = content[u + 4..].trim().trim_matches(|c| c == '\'' || c == '"');
        return Some(html_unescape(url));
    }
    None
}
fn html_unescape(s: &str) -> String {
    s.replace("&amp;", "&").replace("&#38;", "&").replace("&quot;", "\"").replace("&#39;", "'")
}

/// the long link the wiki answered: the Location header of a redirect, else the meta refresh URL. Must be on
/// dungeonsettlers.wiki and contain /builder.
pub fn target_of(status: u16, location: Option<&str>, body: &str) -> Result<String, String> {
    if status == 404 || status == 410 {
        return Err(MSG_GONE.into());
    }
    let raw = match location.filter(|_| (300..400).contains(&status)) {
        Some(l) => l.trim().to_string(),
        None if status == 200 => meta_refresh(body).ok_or_else(|| "The wiki did not answer this short link with a builder link.".to_string())?,
        None => return Err(format!("The wiki could not open this short link (error {status}). Open it on the wiki and copy the long link.")),
    };
    let abs = if raw.starts_with('/') && !raw.starts_with("//") { format!("https://{WIKI_HOST}{raw}") } else { raw };
    let host_ok = abs
        .strip_prefix("https://")
        .map(|r| {
            let host = r.split(['/', '?', '#']).next().unwrap_or("");
            host == WIKI_HOST || host == format!("www.{WIKI_HOST}")
        })
        .unwrap_or(false);
    if !host_ok || !abs.contains("/builder") {
        return Err("This short link does not lead to the wiki builder.".into());
    }
    Ok(abs)
}

/// one GET (no redirects followed, 5 s) -> the long builder link
pub fn resolve(url: &str) -> Result<String, String> {
    if short_url(url).as_deref() != Some(url) {
        return Err("This is not a short wiki link.".into());
    }
    let agent: ureq::Agent = ureq::Agent::config_builder()
        .max_redirects(0)
        .http_status_as_error(false)
        .timeout_global(Some(Duration::from_secs(5)))
        .build()
        .into();
    let mut resp = agent.get(url).call().map_err(|_| MSG_OFFLINE.to_string())?;
    let status = resp.status().as_u16();
    let location = resp.headers().get("location").and_then(|v| v.to_str().ok()).map(str::to_string);
    let body = if location.is_none() && status == 200 {
        resp.body_mut().with_config().limit(256 * 1024).read_to_string().map_err(|_| MSG_OFFLINE.to_string())?
    } else {
        String::new()
    };
    target_of(status, location.as_deref(), &body)
}

/// any builder link, code or short wiki link -> its units (a short link costs one GET)
pub fn decode_any(text: &str) -> Result<Decoded, String> {
    match short_url(text) {
        Some(url) => codec::decode_link(&resolve(&url)?).map_err(|e| e.to_string()),
        None => codec::decode_link(text).map_err(|e| e.to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn short_link_shapes() {
        assert_eq!(short_url("https://dungeonsettlers.wiki/t/abc123").as_deref(), Some("https://dungeonsettlers.wiki/t/abc123"));
        assert_eq!(short_url(" https://dungeonsettlers.wiki/fr/build/Xy_9-z/ ").as_deref(), Some("https://dungeonsettlers.wiki/fr/build/Xy_9-z"));
        assert_eq!(short_url("dungeonsettlers.wiki/pt-br/t/q1").as_deref(), Some("https://dungeonsettlers.wiki/pt-br/t/q1"));
        assert!(short_url("https://dungeonsettlers.wiki/builder?team=AAA").is_none());
        assert!(short_url("https://evil.example/t/abc").is_none());
        assert!(short_url("https://dungeonsettlers.wiki.evil.example/t/abc").is_none());
        assert!(short_url("https://dungeonsettlers.wiki/t/a%2Fb").is_none());
        assert!(short_url("https://dungeonsettlers.wiki/items/sword").is_none());
    }

    #[test]
    fn location_and_meta_refresh() {
        let long = "https://dungeonsettlers.wiki/builder?team=ABC";
        assert_eq!(target_of(302, Some(long), "").unwrap(), long);
        assert_eq!(target_of(301, Some("/fr/builder?team=ABC"), "").unwrap(), "https://dungeonsettlers.wiki/fr/builder?team=ABC");
        let html = r#"<!doctype html><html><head><meta charset="utf-8"><META HTTP-EQUIV="Refresh" CONTENT="0; URL=https://dungeonsettlers.wiki/builder#b=~XYZ&amp;x=1"></head></html>"#;
        assert_eq!(meta_refresh(html).as_deref(), Some("https://dungeonsettlers.wiki/builder#b=~XYZ&x=1"));
        assert_eq!(target_of(200, None, html).unwrap(), "https://dungeonsettlers.wiki/builder#b=~XYZ&x=1");
        let single = "<meta http-equiv='refresh' content='0;url=/builder?team=Q'>";
        assert_eq!(target_of(200, None, single).unwrap(), "https://dungeonsettlers.wiki/builder?team=Q");
        // refused targets
        assert!(target_of(302, Some("https://evil.example/builder?team=A"), "").is_err());
        assert!(target_of(302, Some("//evil.example/builder"), "").is_err());
        assert!(target_of(302, Some("https://dungeonsettlers.wiki/items"), "").is_err());
        assert!(target_of(302, Some("https://dungeonsettlers.wiki.evil.example/builder"), "").is_err());
        assert!(target_of(200, None, "<html>no refresh</html>").is_err());
        assert_eq!(target_of(404, None, "not found").unwrap_err(), MSG_GONE);
        assert!(target_of(500, None, "").unwrap_err().contains("500"));
    }

    #[test]
    fn long_links_stay_offline() {
        // a non-short text never reaches the network: the plain decoder's error comes back
        assert!(decode_any("not a link").is_err());
    }

    /// live: an unknown short id on the real wiki gives the 404 message (needs the network)
    #[test]
    #[ignore]
    fn live_unknown_short_link_is_404() {
        assert_eq!(decode_any("https://dungeonsettlers.wiki/t/zzzzzzzzzz").unwrap_err(), MSG_GONE);
        assert_eq!(decode_any("https://dungeonsettlers.wiki/build/zzzzzzzzzz").unwrap_err(), MSG_GONE);
    }
}
