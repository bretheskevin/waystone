#include "json.h"
#include "base64.h"
#include "jsmn.h"

#include <cstring>
#include <cstdio>

// ---- JSON generation ----

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x",
                             static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string build_raw_tree_json(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::string out = "{\"files\":[";
    for (size_t i = 0; i < files.size(); i++) {
        if (i > 0) out += ',';
        out += "{\"path\":\"";
        out += json_escape(files[i].first);
        out += "\",\"data_b64\":\"";
        out += base64_encode(files[i].second.data(), files[i].second.size());
        out += "\"}";
    }
    out += "]}";
    return out;
}

std::string build_device_head_json(const char* device_id,
                                   const char* hash,
                                   const char* mtime) {
    std::string out = "{\"device_id\":\"";
    out += json_escape(device_id);
    out += "\",\"hash\":\"";
    out += json_escape(hash);
    out += "\",\"mtime\":\"";
    out += json_escape(mtime);
    out += "\"}";
    return out;
}

std::string json_set_mtime(const std::string& json, const char* mtime) {
    // The adapter produces compact JSON: "mtime":"" (no spaces).
    const char* needle = "\"mtime\":\"\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return json;

    std::string result = json.substr(0, pos);
    result += "\"mtime\":\"";
    result += json_escape(mtime);
    result += "\"";
    result += json.substr(pos + strlen(needle));
    return result;
}

// ---- JSON parsing ----

// Max tokens for jsmn. NormalizedSaveDto with many files can be large.
// 2048 tokens handles saves with ~200 files comfortably.
static const int MAX_TOKENS = 2048;

// Helper: compare a jsmn string token against a key.
static bool tok_eq(const char* json, const jsmntok_t& tok, const char* key) {
    if (tok.type != JSMN_STRING) return false;
    size_t len = static_cast<size_t>(tok.end - tok.start);
    return (strlen(key) == len && strncmp(json + tok.start, key, len) == 0);
}

// Parse `json` into a heap-allocated token buffer and return the jsmn count.
// The buffer is heap- rather than stack-resident on purpose: MAX_TOKENS jsmntok_t
// is ~32 KiB, and a stack array that size blows the 3DS's 32 KiB main-thread
// stack (Luma data-abort in wsconfig_load's json_get_string). Fine on the heap.
static int json_parse_tokens(const char* json, std::vector<jsmntok_t>& tokens) {
    tokens.resize(MAX_TOKENS);
    jsmn_parser parser;
    jsmn_init(&parser);
    return jsmn_parse(&parser, json, strlen(json), tokens.data(), MAX_TOKENS);
}

std::string json_get_string(const char* json, const char* key) {
    std::vector<jsmntok_t> tokens;
    int n = json_parse_tokens(json, tokens);
    if (n < 2 || tokens[0].type != JSMN_OBJECT) return "";

    for (int i = 1; i < n - 1; i++) {
        if (tok_eq(json, tokens[i], key) && tokens[i + 1].type == JSMN_STRING) {
            return std::string(json + tokens[i + 1].start,
                               static_cast<size_t>(tokens[i + 1].end - tokens[i + 1].start));
        }
    }
    return "";
}

// Skip over a token and all its descendants. Returns the index of the next sibling.
static int skip_token(const jsmntok_t* tokens, int idx, int total) {
    if (idx >= total) return total;
    if (tokens[idx].type == JSMN_OBJECT) {
        int children = tokens[idx].size;
        int cur = idx + 1;
        for (int c = 0; c < children && cur < total; c++) {
            cur = skip_token(tokens, cur, total);     // skip key
            cur = skip_token(tokens, cur, total);     // skip value
        }
        return cur;
    }
    if (tokens[idx].type == JSMN_ARRAY) {
        int children = tokens[idx].size;
        int cur = idx + 1;
        for (int c = 0; c < children && cur < total; c++) {
            cur = skip_token(tokens, cur, total);
        }
        return cur;
    }
    return idx + 1; // STRING or PRIMITIVE
}

std::string json_get_nested_string(const char* json,
                                   const char* outer_key,
                                   const char* inner_key) {
    std::vector<jsmntok_t> tokens;
    int n = json_parse_tokens(json, tokens);
    if (n < 2 || tokens[0].type != JSMN_OBJECT) return "";

    int top_children = tokens[0].size;
    int i = 1;
    for (int c = 0; c < top_children && i < n - 1; c++) {
        bool is_outer = tok_eq(json, tokens[i], outer_key);
        int val_idx = i + 1;
        if (is_outer && val_idx < n && tokens[val_idx].type == JSMN_OBJECT) {
            // Found the outer object. Search inside it for inner_key.
            int inner_children = tokens[val_idx].size;
            int j = val_idx + 1;
            for (int ic = 0; ic < inner_children && j < n - 1; ic++) {
                if (tok_eq(json, tokens[j], inner_key) &&
                    j + 1 < n && tokens[j + 1].type == JSMN_STRING) {
                    return std::string(
                        json + tokens[j + 1].start,
                        static_cast<size_t>(tokens[j + 1].end - tokens[j + 1].start));
                }
                j++; // skip key
                j = skip_token(tokens.data(), j, n); // skip value
            }
            return ""; // outer found but inner not found
        }
        i++; // skip key
        i = skip_token(tokens.data(), i, n); // skip value
    }
    return "";
}

std::vector<std::string> json_split_array(const char* json) {
    std::vector<std::string> result;
    std::vector<jsmntok_t> tokens;
    int n = json_parse_tokens(json, tokens);
    if (n < 1 || tokens[0].type != JSMN_ARRAY) return result;

    int count = tokens[0].size;
    int idx = 1;
    for (int c = 0; c < count && idx < n; c++) {
        int start = tokens[idx].start;
        // For objects/arrays, tokens[idx].end IS the end of the whole subtree.
        int end = tokens[idx].end;
        result.push_back(std::string(json + start, static_cast<size_t>(end - start)));
        idx = skip_token(tokens.data(), idx, n);
    }
    return result;
}
