#include "net.h"
#include <cstdio>
#include <cstring>
#include <sys/select.h>
#include <curl/curl.h>

// ---- Callbacks ----

struct WriteCtx { std::string buf; };

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t bytes = size * nmemb;
    static_cast<WriteCtx*>(userdata)->buf.append(ptr, bytes);
    return bytes;
}

struct ReadCtx { const uint8_t* data; size_t len; size_t pos; };

static size_t read_cb(char* dest, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<ReadCtx*>(userdata);
    size_t remaining = ctx->len - ctx->pos;
    size_t to_copy = (size * nmemb < remaining) ? size * nmemb : remaining;
    memcpy(dest, ctx->data + ctx->pos, to_copy);
    ctx->pos += to_copy;
    return to_copy;
}

// ---- Helpers ----

static void curl_apply_tls(CURL* c) {
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/cacert.pem");
}

static void set_auth(CURL* c, const WebDavCfg& cfg) {
    curl_apply_tls(c);
    if (cfg.user && cfg.pass) {
        std::string userpwd = std::string(cfg.user) + ":" + cfg.pass;
        curl_easy_setopt(c, CURLOPT_USERPWD, userpwd.c_str());
    }
}

std::string webdav_url(const WebDavCfg& cfg, const char* path) {
    std::string url = cfg.base_url;
    if (!url.empty() && url.back() != '/') url += '/';
    while (path && *path == '/') path++;
    if (path) url += path;
    return url;
}

// ---- Verbs ----

int webdav_put(const WebDavCfg& cfg, const char* path,
               const uint8_t* data, size_t len) {
    CURL* c = curl_easy_init();
    if (!c) return -1;
    std::string url = webdav_url(cfg, path);
    WriteCtx wctx;
    ReadCtx rctx = { data, len, 0 };
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    set_auth(c, cfg);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &wctx);
    curl_easy_setopt(c, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(c, CURLOPT_READFUNCTION, read_cb);
    curl_easy_setopt(c, CURLOPT_READDATA, &rctx);
    curl_easy_setopt(c, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(len));
    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) return -1;
    return (http_code >= 200 && http_code < 300) ? 0 : static_cast<int>(http_code);
}

int webdav_get(const WebDavCfg& cfg, const char* path,
               std::vector<uint8_t>* out) {
    CURL* c = curl_easy_init();
    if (!c) return -1;
    std::string url = webdav_url(cfg, path);
    WriteCtx wctx;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    set_auth(c, cfg);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &wctx);
    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) return -1;
    if (http_code == 404) { out->clear(); return 1; }
    if (http_code < 200 || http_code >= 300) return -1;
    out->assign(wctx.buf.begin(), wctx.buf.end());
    return 0;
}

int webdav_exists(const WebDavCfg& cfg, const char* path) {
    CURL* c = curl_easy_init();
    if (!c) return -1;
    std::string url = webdav_url(cfg, path);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    set_auth(c, cfg);
    curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) return -1;
    if (http_code == 404) return 0;
    return (http_code >= 200 && http_code < 300) ? 1 : -1;
}

int webdav_mkcol(const WebDavCfg& cfg, const char* path) {
    CURL* c = curl_easy_init();
    if (!c) return -1;
    std::string url = webdav_url(cfg, path);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    set_auth(c, cfg);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "MKCOL");
    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) return -1;
    if (http_code == 201 || http_code == 405) return 0; // created or already exists
    return (http_code >= 200 && http_code < 300) ? 0 : static_cast<int>(http_code);
}

int webdav_mkdir_p(const WebDavCfg& cfg, const char* path) {
    std::string p = path;
    std::string current;
    size_t start = 0;
    while (start < p.size()) {
        size_t slash = p.find('/', start);
        if (slash == std::string::npos) slash = p.size();
        std::string seg = p.substr(start, slash - start);
        start = slash + 1;
        if (seg.empty()) continue;
        if (current.empty())
            current = seg;
        else
            current += "/" + seg;
        int rc = webdav_mkcol(cfg, current.c_str());
        if (rc != 0) return rc;
    }
    return 0;
}

int webdav_propfind(const WebDavCfg& cfg, const char* path,
                    std::vector<std::string>* out_hrefs) {
    out_hrefs->clear();
    CURL* c = curl_easy_init();
    if (!c) return -1;
    std::string url = webdav_url(cfg, path);
    WriteCtx wctx;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    set_auth(c, cfg);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &wctx);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PROPFIND");
    const char* body =
        "<?xml version=\"1.0\"?>"
        "<D:propfind xmlns:D=\"DAV:\"><D:prop><D:resourcetype/></D:prop></D:propfind>";
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(strlen(body)));
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Depth: 1");
    hdrs = curl_slist_append(hdrs, "Content-Type: application/xml");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) return -1;
    if (http_code == 404) return 0;
    if (http_code < 200 || http_code >= 400) return -1;

    // Parse hrefs from XML response
    const std::string& xml = wctx.buf;
    size_t pos = 0;
    while ((pos = xml.find("href>", pos)) != std::string::npos) {
        pos += 5; // skip "href>"
        size_t end = xml.find('<', pos);
        if (end == std::string::npos) break;
        std::string href = xml.substr(pos, end - pos);
        size_t hstart = href.find_first_not_of(" \t\r\n");
        size_t hend = href.find_last_not_of(" \t\r\n");
        if (hstart != std::string::npos && hend != std::string::npos) {
            out_hrefs->push_back(href.substr(hstart, hend - hstart + 1));
        }
        pos = end;
    }
    return 0;
}
