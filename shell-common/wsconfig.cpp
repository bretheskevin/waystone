#include "wsconfig.h"
#include "json.h"

#include <cstdio>
#include <cstring>

// Unescape a raw jsmn string token (json_get_string returns raw JSON text).
static std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            i++;
            switch (s[i]) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':
                    if (i + 4 < s.size()) {
                        unsigned cp = 0;
                        for (int j = 1; j <= 4; j++) {
                            char c = s[i + j];
                            cp <<= 4;
                            if (c >= '0' && c <= '9') cp |= static_cast<unsigned>(c - '0');
                            else if (c >= 'a' && c <= 'f') cp |= static_cast<unsigned>(c - 'a' + 10);
                            else if (c >= 'A' && c <= 'F') cp |= static_cast<unsigned>(c - 'A' + 10);
                        }
                        i += 4;
                        if (cp < 0x80) out += static_cast<char>(cp);
                        // non-ASCII \uXXXX not needed for config fields; silently dropped
                    }
                    break;
                default: out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

WaystoneShellConfig wsconfig_load(const char* path) {
    WaystoneShellConfig cfg;

    FILE* f = fopen(path, "rb");
    if (!f) return cfg;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        fclose(f);
        return cfg;
    }

    std::string buf(static_cast<size_t>(len), '\0');
    size_t got = fread(&buf[0], 1, static_cast<size_t>(len), f);
    fclose(f);
    if (got != static_cast<size_t>(len)) return cfg;

    cfg.server_url = json_unescape(json_get_string(buf.c_str(), "server_url"));
    cfg.username   = json_unescape(json_get_string(buf.c_str(), "username"));

    std::string policy_str = json_get_string(buf.c_str(), "conflict_policy");
    if (policy_str == "prompt")
        cfg.conflict_policy = WsConflictPolicy::Prompt;
    else
        cfg.conflict_policy = WsConflictPolicy::NewestWins;

    // safety_backup is stored as a quoted string ("true"/"false") because
    // json_get_string only matches JSMN_STRING tokens, not JSMN_PRIMITIVE.
    std::string backup_str = json_get_string(buf.c_str(), "safety_backup");
    if (backup_str == "false")
        cfg.safety_backup = false;
    else
        cfg.safety_backup = true;

    return cfg;
}

bool wsconfig_save(const WaystoneShellConfig& cfg, const char* path) {
    std::string policy_val = (cfg.conflict_policy == WsConflictPolicy::Prompt)
                                 ? "prompt" : "newest-wins";
    std::string backup_val = cfg.safety_backup ? "true" : "false";

    std::string json = "{\n"
        "  \"server_url\": \"" + json_escape(cfg.server_url) + "\",\n"
        "  \"username\": \"" + json_escape(cfg.username) + "\",\n"
        "  \"conflict_policy\": \"" + policy_val + "\",\n"
        "  \"safety_backup\": \"" + backup_val + "\"\n"
        "}";

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t written = fwrite(json.data(), 1, json.size(), f);
    fclose(f);
    return written == json.size();
}
