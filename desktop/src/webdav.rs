use anyhow::{Context, Result, bail};
use reqwest::Client;
use zeroize::Zeroize;

pub struct WebDavClient {
    client: Client,
    base_url: String,
    username: Option<String>,
    password: Option<String>,
}

impl std::fmt::Debug for WebDavClient {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("WebDavClient")
            .field("base_url", &self.base_url)
            .field("username", &self.username)
            .field("password", &"[REDACTED]")
            .finish()
    }
}

impl WebDavClient {
    pub fn new(base_url: &str, username: Option<String>, password: Option<String>) -> Self {
        Self {
            client: Client::builder()
                .connect_timeout(std::time::Duration::from_secs(10))
                .timeout(std::time::Duration::from_secs(120))
                .build()
                .expect("failed to build HTTP client"),
            base_url: base_url.trim_end_matches('/').to_string(),
            username,
            password,
        }
    }

    fn url(&self, path: &str) -> String {
        format!("{}/{}", self.base_url, path.trim_start_matches('/'))
    }

    fn request(&self, method: reqwest::Method, path: &str) -> reqwest::RequestBuilder {
        let mut req = self.client.request(method, self.url(path));
        if let (Some(u), Some(p)) = (&self.username, &self.password) {
            req = req.basic_auth(u, Some(p));
        }
        req
    }

    pub async fn put(&self, path: &str, body: Vec<u8>) -> Result<()> {
        let resp = self
            .request(reqwest::Method::PUT, path)
            .body(body)
            .send()
            .await
            .context("WebDAV PUT failed")?;
        if !resp.status().is_success() {
            bail!("PUT {} returned {}", path, resp.status());
        }
        Ok(())
    }

    pub async fn get(&self, path: &str) -> Result<Option<Vec<u8>>> {
        let resp = self
            .request(reqwest::Method::GET, path)
            .send()
            .await
            .context("WebDAV GET failed")?;
        if resp.status().as_u16() == 404 {
            return Ok(None);
        }
        if !resp.status().is_success() {
            bail!("GET {} returned {}", path, resp.status());
        }
        Ok(Some(resp.bytes().await?.to_vec()))
    }

    pub async fn mkcol(&self, path: &str) -> Result<()> {
        let resp = self
            .request(reqwest::Method::from_bytes(b"MKCOL").unwrap(), path)
            .send()
            .await
            .context("WebDAV MKCOL failed")?;
        let status = resp.status().as_u16();
        if status != 201 && status != 405 && !resp.status().is_success() {
            bail!("MKCOL {} returned {}", path, resp.status());
        }
        Ok(())
    }

    #[allow(dead_code)]
    pub async fn exists(&self, path: &str) -> Result<bool> {
        let resp = self
            .request(reqwest::Method::HEAD, path)
            .send()
            .await
            .context("WebDAV HEAD failed")?;
        Ok(resp.status().is_success())
    }

    pub async fn mkdir_p(&self, path: &str) -> Result<()> {
        let segments: Vec<&str> = path.split('/').filter(|s| !s.is_empty()).collect();
        let mut current = String::new();
        for seg in segments {
            current = format!("{}/{}", current, seg);
            self.mkcol(&current).await?;
        }
        Ok(())
    }

    /// Issue a PROPFIND Depth:1 against a collection and return all `<href>` values.
    #[allow(dead_code)]
    pub async fn propfind(&self, path: &str) -> Result<Vec<String>> {
        let resp = self
            .request(reqwest::Method::from_bytes(b"PROPFIND").unwrap(), path)
            .header("Depth", "1")
            .header("Content-Type", "application/xml")
            .body(
                r#"<?xml version="1.0"?><D:propfind xmlns:D="DAV:"><D:prop><D:resourcetype/></D:prop></D:propfind>"#,
            )
            .send()
            .await
            .context("WebDAV PROPFIND failed")?;

        if resp.status().as_u16() == 404 {
            return Ok(vec![]);
        }
        if !resp.status().is_success() {
            bail!("PROPFIND {} returned {}", path, resp.status());
        }
        let text = resp.text().await?;
        Ok(parse_propfind_hrefs(&text))
    }
}

impl Drop for WebDavClient {
    fn drop(&mut self) {
        self.password.zeroize();
    }
}

fn parse_propfind_hrefs(xml: &str) -> Vec<String> {
    // Split on every occurrence of "href>" (opening and closing tags).
    // Odd-indexed segments (1, 3, …) are the text between an opening <*:href> and the next <.
    let mut hrefs = Vec::new();
    for part in xml.split("href>").skip(1).step_by(2) {
        if let Some(end) = part.find('<') {
            let href = part[..end].trim();
            if !href.is_empty() {
                hrefs.push(href.to_string());
            }
        }
    }
    hrefs
}

pub struct BlockingWebDav {
    client: reqwest::blocking::Client,
    base_url: String,
    username: Option<String>,
    password: Option<String>,
}

impl std::fmt::Debug for BlockingWebDav {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("BlockingWebDav")
            .field("base_url", &self.base_url)
            .field("username", &self.username)
            .field("password", &"[REDACTED]")
            .finish()
    }
}

impl Clone for BlockingWebDav {
    fn clone(&self) -> Self {
        Self {
            client: reqwest::blocking::Client::builder()
                .connect_timeout(std::time::Duration::from_secs(10))
                .timeout(std::time::Duration::from_secs(120))
                .build()
                .expect("failed to build HTTP client"),
            base_url: self.base_url.clone(),
            username: self.username.clone(),
            password: self.password.clone(),
        }
    }
}

impl BlockingWebDav {
    pub fn new(base_url: &str, username: Option<String>, password: Option<String>) -> Self {
        Self {
            client: reqwest::blocking::Client::builder()
                .connect_timeout(std::time::Duration::from_secs(10))
                .timeout(std::time::Duration::from_secs(120))
                .build()
                .expect("failed to build HTTP client"),
            base_url: base_url.trim_end_matches('/').to_string(),
            username,
            password,
        }
    }

    fn url(&self, path: &str) -> String {
        format!("{}/{}", self.base_url, path.trim_start_matches('/'))
    }

    fn request(&self, method: reqwest::Method, path: &str) -> reqwest::blocking::RequestBuilder {
        let mut req = self.client.request(method, self.url(path));
        if let (Some(u), Some(p)) = (&self.username, &self.password) {
            req = req.basic_auth(u, Some(p));
        }
        req
    }
}

impl Drop for BlockingWebDav {
    fn drop(&mut self) {
        self.password.zeroize();
    }
}

impl waystone_sync::WebDav for BlockingWebDav {
    fn get(&self, path: &str) -> waystone_sync::Result<Option<Vec<u8>>> {
        let resp = self
            .request(reqwest::Method::GET, path)
            .send()
            .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))?;
        if resp.status().as_u16() == 404 {
            return Ok(None);
        }
        if !resp.status().is_success() {
            return Err(waystone_sync::SyncError::WebDav(format!(
                "GET {} returned {}",
                path,
                resp.status()
            )));
        }
        Ok(Some(
            resp.bytes()
                .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))?
                .to_vec(),
        ))
    }

    fn put(&self, path: &str, body: Vec<u8>) -> waystone_sync::Result<()> {
        let resp = self
            .request(reqwest::Method::PUT, path)
            .body(body)
            .send()
            .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))?;
        if !resp.status().is_success() {
            return Err(waystone_sync::SyncError::WebDav(format!(
                "PUT {} returned {}",
                path,
                resp.status()
            )));
        }
        Ok(())
    }

    fn exists(&self, path: &str) -> waystone_sync::Result<bool> {
        let resp = self
            .request(reqwest::Method::HEAD, path)
            .send()
            .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))?;
        Ok(resp.status().is_success())
    }

    fn propfind(&self, path: &str) -> waystone_sync::Result<Vec<String>> {
        let resp = self
            .request(reqwest::Method::from_bytes(b"PROPFIND").unwrap(), path)
            .header("Depth", "1")
            .header("Content-Type", "application/xml")
            .body(
                r#"<?xml version="1.0"?><D:propfind xmlns:D="DAV:"><D:prop><D:resourcetype/></D:prop></D:propfind>"#,
            )
            .send()
            .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))?;
        if resp.status().as_u16() == 404 {
            return Ok(vec![]);
        }
        if !resp.status().is_success() {
            return Err(waystone_sync::SyncError::WebDav(format!(
                "PROPFIND {} returned {}",
                path,
                resp.status()
            )));
        }
        let text = resp
            .text()
            .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))?;
        Ok(parse_propfind_hrefs(&text))
    }

    fn mkdir_p(&self, path: &str) -> waystone_sync::Result<()> {
        let segments: Vec<&str> = path.split('/').filter(|s| !s.is_empty()).collect();
        let mut current = String::new();
        for seg in segments {
            current = format!("{}/{}", current, seg);
            let resp = self
                .request(reqwest::Method::from_bytes(b"MKCOL").unwrap(), &current)
                .send()
                .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))?;
            let status = resp.status().as_u16();
            if status != 201 && status != 405 && !resp.status().is_success() {
                return Err(waystone_sync::SyncError::WebDav(format!(
                    "MKCOL {} returned {}",
                    current,
                    resp.status()
                )));
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_propfind_hrefs_extracts_child_hrefs() {
        let xml = r#"<?xml version="1.0"?>
<D:multistatus xmlns:D="DAV:">
  <D:response>
    <D:href>/data/heads/</D:href>
    <D:propstat><D:status>HTTP/1.1 200 OK</D:status></D:propstat>
  </D:response>
  <D:response>
    <D:href>/data/heads/device1.json</D:href>
    <D:propstat><D:status>HTTP/1.1 200 OK</D:status></D:propstat>
  </D:response>
  <D:response>
    <D:href>/data/heads/device2.json</D:href>
    <D:propstat><D:status>HTTP/1.1 200 OK</D:status></D:propstat>
  </D:response>
</D:multistatus>"#;
        let hrefs = parse_propfind_hrefs(xml);
        assert_eq!(hrefs.len(), 3);
        assert!(hrefs.iter().any(|h| h == "/data/heads/"));
        assert!(hrefs.iter().any(|h| h == "/data/heads/device1.json"));
        assert!(hrefs.iter().any(|h| h == "/data/heads/device2.json"));
    }

    #[tokio::test]
    #[ignore]
    async fn webdav_put_get_round_trip() {
        let client = WebDavClient::new("http://localhost:5000", None, None);
        client.mkdir_p("/test").await.unwrap();
        client
            .put("/test/hello.txt", b"world".to_vec())
            .await
            .unwrap();
        let data = client.get("/test/hello.txt").await.unwrap().unwrap();
        assert_eq!(data, b"world");
    }

    #[tokio::test]
    #[ignore]
    async fn webdav_get_missing_returns_none() {
        let client = WebDavClient::new("http://localhost:5000", None, None);
        let data = client.get("/nonexistent/file.bin").await.unwrap();
        assert!(data.is_none());
    }

    #[tokio::test]
    #[ignore]
    async fn webdav_propfind_lists_children() {
        let client = WebDavClient::new("http://localhost:5000", None, None);
        client.mkdir_p("/proptest/col").await.unwrap();
        client
            .put("/proptest/col/a.txt", b"a".to_vec())
            .await
            .unwrap();
        client
            .put("/proptest/col/b.txt", b"b".to_vec())
            .await
            .unwrap();
        let hrefs = client.propfind("/proptest/col").await.unwrap();
        assert!(hrefs.iter().any(|h| h.ends_with("/a.txt")));
        assert!(hrefs.iter().any(|h| h.ends_with("/b.txt")));
    }

    #[test]
    fn blocking_webdav_implements_sync_trait() {
        let dav = BlockingWebDav::new("http://localhost:5099", None, None);
        let _trait_obj: &dyn waystone_sync::WebDav = &dav;
    }
}
