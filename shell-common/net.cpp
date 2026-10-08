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

struct XferCtx {
    bool (*progress)(size_t got, size_t total, void* ctx);
    void* ctx;
    bool upload;
};

static int xfer_cb(void* userdata, curl_off_t dltotal, curl_off_t dlnow,
                   curl_off_t ultotal, curl_off_t ulnow) {
    auto* x = static_cast<XferCtx*>(userdata);
    if (!x->progress) return 0;
    curl_off_t now   = x->upload ? ulnow : dlnow;
    curl_off_t total = x->upload ? ultotal : dltotal;
    return x->progress(static_cast<size_t>(now), static_cast<size_t>(total),
                       x->ctx) ? 0 : 1;
}

// ---- Helpers ----

static void curl_apply_tls(CURL* c) {
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/cacert.pem");
}

// Applies timeouts + TLS to every curl handle. Called by all verbs via set_auth.
static void curl_apply_common(CURL* c) {
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_apply_tls(c);
}

static void set_auth(CURL* c, const WebDavCfg& cfg) {
    curl_apply_common(c);
    if (cfg.user && cfg.pass) {
        std::string userpwd = std::string(cfg.user) + ":" + cfg.pass;
        curl_easy_setopt(c, CURLOPT_USERPWD, userpwd.c_str());
    }
}

static void apply_progress(CURL* c, XferCtx* x) {
    if (!x->progress) return;
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xfer_cb);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, x);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
}

std::string webdav_url(const WebDavCfg& cfg, const char* path) {
    std::string url = cfg.base_url;
    if (!url.empty() && url.back() != '/') url += '/';
    while (path && *path == '/') path++;
    if (path) url += path;
    return url;
}

// ---- Session ----

struct WebDavSession {
    CURL* curl;
    std::string base_url, user, pass;
    bool has_auth; // WebDavCfg allows null user/pass; empty string != null
    long num_connects;
};

static WebDavCfg session_cfg(const WebDavSession* s) {
    WebDavCfg cfg;
    cfg.base_url = s->base_url.c_str();
    cfg.user = s->has_auth ? s->user.c_str() : nullptr;
    cfg.pass = s->has_auth ? s->pass.c_str() : nullptr;
    return cfg;
}

WebDavSession* webdav_session_begin(const WebDavCfg& cfg) {
    WebDavSession* s = new WebDavSession();
    s->curl = curl_easy_init();
    if (!s->curl) {
        printf("[net] webdav session begin FAILED (curl_easy_init)\n");
        delete s;
        return nullptr;
    }
    s->base_url = cfg.base_url ? cfg.base_url : "";
    s->user = cfg.user ? cfg.user : "";
    s->pass = cfg.pass ? cfg.pass : "";
    s->has_auth = cfg.user && cfg.pass;
    s->num_connects = 0;
    printf("[net] webdav session begin: %s\n", s->base_url.c_str());
    return s;
}

void webdav_session_end(WebDavSession* s) {
    if (!s) return;
    printf("[net] webdav session end: %s num_connects=%ld\n",
           s->base_url.c_str(), s->num_connects);
    curl_easy_cleanup(s->curl);
    delete s;
}

long webdav_session_num_connects(const WebDavSession* s) {
    return s ? s->num_connects : 0;
}

// curl_easy_reset preserves the connection cache, DNS cache, and TLS
// session-ID cache (documented libcurl behavior) while clearing per-request
// options -- this is what makes reuse safe against option leakage
// (CUSTOMREQUEST/NOBODY/UPLOAD/POSTFIELDS from the previous request must not
// bleed into the next one). After each perform, accumulate how many NEW
// connections that transfer opened.
static void session_note_connects(WebDavSession* s) {
    long nc = 0;
    curl_easy_getinfo(s->curl, CURLINFO_NUM_CONNECTS, &nc);
    s->num_connects += nc;
}

// ---- Verbs ----

static int put_impl(CURL* c, const WebDavCfg& cfg, const char* path,
                    const uint8_t* data, size_t len,
                    bool (*progress)(size_t, size_t, void*), void* ctx) {
    std::string url = webdav_url(cfg, path);
    WriteCtx wctx;
    ReadCtx rctx = { data, len, 0 };
    XferCtx xctx = { progress, ctx, true };
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    set_auth(c, cfg);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &wctx);
    curl_easy_setopt(c, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(c, CURLOPT_READFUNCTION, read_cb);
    curl_easy_setopt(c, CURLOPT_READDATA, &rctx);
    curl_easy_setopt(c, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(len));
    apply_progress(c, &xctx);
    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    if (res != CURLE_OK) {
        printf("[net] webdav_put %s (%zu bytes) transport error: %s\n",
               path, len, curl_easy_strerror(res));
        return -1;
    }
    if (http_code < 200 || http_code >= 300) {
        printf("[net] webdav_put %s (%zu bytes) status=%ld\n", path, len, http_code);
        return static_cast<int>(http_code);
    }
    return 0;
}

int webdav_put(const WebDavCfg& cfg, const char* path,
               const uint8_t* data, size_t len,
               bool (*progress)(size_t got, size_t total, void* ctx),
               void* ctx) {
    CURL* c = curl_easy_init();
    if (!c) return -1;
    int rc = put_impl(c, cfg, path, data, len, progress, ctx);
    curl_easy_cleanup(c);
    return rc;
}

int webdav_put_s(WebDavSession* s, const char* path,
                 const uint8_t* data, size_t len,
                 bool (*progress)(size_t got, size_t total, void* ctx),
                 void* ctx) {
    if (!s) return -1;
    curl_easy_reset(s->curl);
    int rc = put_impl(s->curl, session_cfg(s), path, data, len, progress, ctx);
    session_note_connects(s);
    return rc;
}

static int get_impl(CURL* c, const WebDavCfg& cfg, const char* path,
                    std::vector<uint8_t>* out,
                    bool (*progress)(size_t, size_t, void*), void* ctx) {
    std::string url = webdav_url(cfg, path);
    WriteCtx wctx;
    XferCtx xctx = { progress, ctx, false };
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    set_auth(c, cfg);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &wctx);
    apply_progress(c, &xctx);
    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    if (res != CURLE_OK) {
        printf("[net] webdav_get %s transport error: %s\n", path, curl_easy_strerror(res));
        return -1;
    }
    if (http_code == 404) { out->clear(); return 1; }
    if (http_code < 200 || http_code >= 300) {
        printf("[net] webdav_get %s status=%ld\n", path, http_code);
        return -1;
    }
    out->assign(wctx.buf.begin(), wctx.buf.end());
    return 0;
}

int webdav_get(const WebDavCfg& cfg, const char* path,
               std::vector<uint8_t>* out,
               bool (*progress)(size_t got, size_t total, void* ctx),
               void* ctx) {
    CURL* c = curl_easy_init();
    if (!c) return -1;
    int rc = get_impl(c, cfg, path, out, progress, ctx);
    curl_easy_cleanup(c);
    return rc;
}

int webdav_get_s(WebDavSession* s, const char* path,
                 std::vector<uint8_t>* out,
                 bool (*progress)(size_t got, size_t total, void* ctx),
                 void* ctx) {
    if (!s) return -1;
    curl_easy_reset(s->curl);
    int rc = get_impl(s->curl, session_cfg(s), path, out, progress, ctx);
    session_note_connects(s);
    return rc;
}

static int exists_impl(CURL* c, const WebDavCfg& cfg, const char* path) {
    std::string url = webdav_url(cfg, path);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    set_auth(c, cfg);
    curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    if (res != CURLE_OK) return -1;
    if (http_code == 404) return 0;
    return (http_code >= 200 && http_code < 300) ? 1 : -1;
}

int webdav_exists(const WebDavCfg& cfg, const char* path) {
    CURL* c = curl_easy_init();
    if (!c) return -1;
    int rc = exists_impl(c, cfg, path);
    curl_easy_cleanup(c);
    return rc;
}

int webdav_exists_s(WebDavSession* s, const char* path) {
    if (!s) return -1;
    curl_easy_reset(s->curl);
    int rc = exists_impl(s->curl, session_cfg(s), path);
    session_note_connects(s);
    return rc;
}

static int mkcol_impl(CURL* c, const WebDavCfg& cfg, const char* path) {
    std::string url = webdav_url(cfg, path);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    set_auth(c, cfg);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "MKCOL");
    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    if (res != CURLE_OK) return -1;
    if (http_code == 201 || http_code == 405) return 0; // created or already exists
    return (http_code >= 200 && http_code < 300) ? 0 : static_cast<int>(http_code);
}

int webdav_mkcol(const WebDavCfg& cfg, const char* path) {
    CURL* c = curl_easy_init();
    if (!c) return -1;
    int rc = mkcol_impl(c, cfg, path);
    curl_easy_cleanup(c);
    return rc;
}

int webdav_mkcol_s(WebDavSession* s, const char* path) {
    if (!s) return -1;
    curl_easy_reset(s->curl);
    int rc = mkcol_impl(s->curl, session_cfg(s), path);
    session_note_connects(s);
    return rc;
}

int webdav_mkdir_p(const WebDavCfg& cfg, const char* path) {
    WebDavSession* s = webdav_session_begin(cfg);
    if (!s) return -1;
    int rc = webdav_mkdir_p_s(s, path);
    webdav_session_end(s);
    return rc;
}

int webdav_mkdir_p_s(WebDavSession* s, const char* path) {
    if (!s) return -1;
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
        int rc = webdav_mkcol_s(s, current.c_str());
        if (rc != 0) return rc;
    }
    return 0;
}

static int propfind_impl(CURL* c, const WebDavCfg& cfg, const char* path,
                         std::vector<std::string>* out_hrefs) {
    out_hrefs->clear();
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

int webdav_propfind(const WebDavCfg& cfg, const char* path,
                    std::vector<std::string>* out_hrefs) {
    CURL* c = curl_easy_init();
    if (!c) return -1;
    int rc = propfind_impl(c, cfg, path, out_hrefs);
    curl_easy_cleanup(c);
    return rc;
}

int webdav_propfind_s(WebDavSession* s, const char* path,
                      std::vector<std::string>* out_hrefs) {
    if (!s) return -1;
    curl_easy_reset(s->curl);
    int rc = propfind_impl(s->curl, session_cfg(s), path, out_hrefs);
    session_note_connects(s);
    return rc;
}

int http_get(const char* url, std::string* out) {
    CURL* c = curl_easy_init();
    if (!c) return -1;
    WriteCtx wctx;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &wctx);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    // GitHub API 403s the default curl UA — documented behavior change for all
    // http_get callers, required for api.github.com.
    curl_easy_setopt(c, CURLOPT_USERAGENT, "waystone");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_apply_tls(c);
    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) {
        printf("[net] http_get %s transport error: %s\n", url, curl_easy_strerror(res));
        return -1;
    }
    printf("[net] http_get %s status=%ld bytes=%zu\n", url, http_code,
           wctx.buf.size());
    if (http_code == 200) {
        *out = std::move(wctx.buf);
    }
    return static_cast<int>(http_code);
}

// ---- Streaming download ----

struct FileWriteCtx { FILE* fp; bool io_error; };

static size_t file_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<FileWriteCtx*>(userdata);
    size_t bytes = size * nmemb;
    if (fwrite(ptr, 1, bytes, ctx->fp) != bytes) {
        ctx->io_error = true;
        return 0;  // abort the transfer on disk-full / write error
    }
    return bytes;
}

int http_download(const char* url, const char* dest_path,
                  bool (*progress)(size_t got, size_t total, void* ctx),
                  void* ctx) {
    printf("[net] http_download %s -> %s\n", url, dest_path);
    FILE* fp = fopen(dest_path, "wb");
    if (!fp) {
        printf("[net] http_download: cannot open %s for writing\n", dest_path);
        return -1;
    }
    CURL* c = curl_easy_init();
    if (!c) {
        fclose(fp);
        remove(dest_path);
        return -1;
    }
    FileWriteCtx wctx = { fp, false };
    XferCtx xctx = { progress, ctx, false };
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "waystone");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, file_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &wctx);
    apply_progress(c, &xctx);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
    // Total timeout 120s (not the 20s used by the WebDAV verbs): a ~1.5MB .3dsx
    // on slow 3DS Wi-Fi can far exceed 20s; a stalled transfer still can't hang
    // the worker forever.
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 120L);
    curl_apply_tls(c);
    CURLcode res = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);
    fclose(fp);
    if (res != CURLE_OK || wctx.io_error) {
        printf("[net] http_download FAILED (%s): %s\n",
               wctx.io_error ? "disk write error" : curl_easy_strerror(res), url);
        remove(dest_path);
        return -1;
    }
    printf("[net] http_download status=%ld: %s\n", http_code, url);
    if (http_code != 200) {
        remove(dest_path);
        return static_cast<int>(http_code);
    }
    return 0;
}
