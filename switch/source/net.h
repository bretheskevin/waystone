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
int webdav_put(const WebDavCfg& cfg, const char* path,
               const uint8_t* data, size_t len);

// GET binary data from path. Returns 0 on success, 1 on 404 (out empty), -1 on error.
int webdav_get(const WebDavCfg& cfg, const char* path,
               std::vector<uint8_t>* out);

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

#endif
