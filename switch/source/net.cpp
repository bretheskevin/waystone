#include "net.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <curl/curl.h>

struct WriteCtx { std::string buf; };

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t bytes = size * nmemb;
    static_cast<WriteCtx*>(userdata)->buf.append(ptr, bytes);
    return bytes;
}

struct ReadCtx { const char* data; size_t len; size_t pos; };

static size_t read_cb(char* dest, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<ReadCtx*>(userdata);
    size_t remaining = ctx->len - ctx->pos;
    size_t to_copy = (size * nmemb < remaining) ? size * nmemb : remaining;
    memcpy(dest, ctx->data + ctx->pos, to_copy);
    ctx->pos += to_copy;
    return to_copy;
}

int net_webdav_probe(const char* base_url, const char* user, const char* pass) {
    CURLcode res;
    long http_code = 0;
    int ret = 0;
    std::string userpwd = std::string(user) + ":" + pass;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        printf("curl_global_init failed\n");
        return 1;
    }

    std::string probe_url = std::string(base_url) + "/.waystone-probe";
    const char* probe_body = "waystone-net-spike";
    size_t probe_len = strlen(probe_body);

    // PUT
    {
        WriteCtx wctx;
        ReadCtx rctx = { probe_body, probe_len, 0 };
        CURL* c = curl_easy_init();
        if (!c) { printf("PUT: curl_easy_init failed\n"); ret = 1; goto done; }
        curl_easy_setopt(c, CURLOPT_URL, probe_url.c_str());
        curl_easy_setopt(c, CURLOPT_USERPWD, userpwd.c_str());
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &wctx);
        curl_easy_setopt(c, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(c, CURLOPT_READFUNCTION, read_cb);
        curl_easy_setopt(c, CURLOPT_READDATA, &rctx);
        curl_easy_setopt(c, CURLOPT_INFILESIZE_LARGE, (curl_off_t)probe_len);
        res = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
        printf("PUT %s -> curl=%d http=%ld\n", probe_url.c_str(), (int)res, http_code);
        curl_easy_cleanup(c);
        if (res != CURLE_OK) { printf("PUT: %s\n", curl_easy_strerror(res)); ret = 1; goto done; }
    }

    // GET
    {
        WriteCtx wctx;
        CURL* c = curl_easy_init();
        if (!c) { printf("GET: curl_easy_init failed\n"); ret = 1; goto done; }
        curl_easy_setopt(c, CURLOPT_URL, probe_url.c_str());
        curl_easy_setopt(c, CURLOPT_USERPWD, userpwd.c_str());
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &wctx);
        res = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
        printf("GET %s -> curl=%d http=%ld body=%zu bytes\n",
               probe_url.c_str(), (int)res, http_code, wctx.buf.size());
        curl_easy_cleanup(c);
        if (res != CURLE_OK) { printf("GET: %s\n", curl_easy_strerror(res)); ret = 1; goto done; }
        bool match = (wctx.buf.size() == probe_len &&
                      memcmp(wctx.buf.data(), probe_body, probe_len) == 0);
        printf("GET round-trip match=%d\n", (int)match);
        if (!match) ret = 1;
    }

    // PROPFIND Depth:1
    {
        WriteCtx wctx;
        std::string pf_url = std::string(base_url) + "/";
        CURL* c = curl_easy_init();
        if (!c) { printf("PROPFIND: curl_easy_init failed\n"); ret = 1; goto done; }
        curl_easy_setopt(c, CURLOPT_URL, pf_url.c_str());
        curl_easy_setopt(c, CURLOPT_USERPWD, userpwd.c_str());
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &wctx);
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PROPFIND");
        const char* pf_body =
            "<?xml version=\"1.0\"?>"
            "<D:propfind xmlns:D=\"DAV:\"><D:prop><D:resourcetype/></D:prop></D:propfind>";
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, pf_body);
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)strlen(pf_body));
        struct curl_slist* hdrs = nullptr;
        hdrs = curl_slist_append(hdrs, "Depth: 1");
        hdrs = curl_slist_append(hdrs, "Content-Type: application/xml");
        if (!hdrs) {
            printf("PROPFIND: header alloc failed\n");
            curl_easy_cleanup(c);
            ret = 1;
            goto done;
        }
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
        res = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
        printf("PROPFIND %s -> curl=%d http=%ld\n", pf_url.c_str(), (int)res, http_code);
        printf("PROPFIND body (%zu bytes):\n%s\n", wctx.buf.size(), wctx.buf.c_str());
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);
        if (res != CURLE_OK) { printf("PROPFIND: %s\n", curl_easy_strerror(res)); ret = 1; }
    }

done:
    curl_global_cleanup();
    return ret;
}
