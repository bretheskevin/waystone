#ifndef WAYSTONE_NET_H
#define WAYSTONE_NET_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct WebDavCfg {
    const char* base_url; // e.g. "http://host:5005"
    const char* user;
    const char* pass;
};

// Build a full URL: base_url + "/" + path (strips leading / from path).
std::string webdav_url(const WebDavCfg& cfg, const char* path);

// PUT binary data to path. Returns 0 on success (HTTP 2xx), nonzero on error.
// progress(got, total, ctx) (optional) reports upload bytes on the calling thread; returning
// false aborts the transfer (-1).
int webdav_put(const WebDavCfg& cfg, const char* path,
               const uint8_t* data, size_t len,
               bool (*progress)(size_t got, size_t total, void* ctx) = nullptr,
               void* ctx = nullptr);

// GET binary data from path. Returns 0 on success, 1 on 404 (out empty), -1 on error.
// progress (optional) reports download bytes; total is 0 when the server sends no length.
int webdav_get(const WebDavCfg& cfg, const char* path,
               std::vector<uint8_t>* out,
               bool (*progress)(size_t got, size_t total, void* ctx) = nullptr,
               void* ctx = nullptr);

// HEAD check existence. Returns 1 if exists, 0 if 404, -1 on error.
int webdav_exists(const WebDavCfg& cfg, const char* path);

// MKCOL create collection. Returns 0 on success or 405 (already exists), nonzero on error.
int webdav_mkcol(const WebDavCfg& cfg, const char* path);

// Create nested directories (MKCOL each segment). Returns 0 on success.
int webdav_mkdir_p(const WebDavCfg& cfg, const char* path);

// PROPFIND Depth:1. Returns hrefs in out_hrefs. Returns 0 on success, nonzero on error.
// On 404, returns 0 with empty out_hrefs.
int webdav_propfind(const WebDavCfg& cfg, const char* path,
                    std::vector<std::string>* out_hrefs);

// Unauthenticated HTTPS GET. On HTTP 200, writes body into *out and returns 200.
// Returns the HTTP status code on successful transport (non-200 = fetch failed).
// Returns -1 on curl/transport error. Follows redirects; TLS via romfs:/cacert.pem.
// Sends User-Agent "waystone-3ds" (GitHub API 403s the default curl UA).
int http_get(const char* url, std::string* out);

// Streaming download: response body is written to dest_path as it arrives (no
// full-file buffering). Unauthenticated HTTPS, redirects followed, same TLS.
// progress(got, total, ctx) is called on the curl thread; returning false aborts.
// Returns 0 on HTTP 200, the HTTP status code on completed transport with a
// non-200 status, -1 on curl/IO error. On ANY failure dest_path is removed.
int http_download(const char* url, const char* dest_path,
                  bool (*progress)(size_t got, size_t total, void* ctx),
                  void* ctx);

#endif
