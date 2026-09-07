#include "base64.h"

static const char TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);

        out += TABLE[(n >> 18) & 0x3F];
        out += TABLE[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? TABLE[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? TABLE[n & 0x3F] : '=';
    }
    return out;
}

// Reverse-map one base64 character to its 6-bit value.
// Returns -1 for characters that are not part of the STANDARD alphabet.
static int8_t b64_decode_char(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<int8_t>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<uint8_t> base64_decode(const std::string& in) {
    std::vector<uint8_t> out;
    const size_t n = in.size();

    if (n == 0) return out;

    // Standard base64 output is always a multiple of 4 bytes (with = padding).
    if (n % 4 != 0) return out; // invalid — return empty

    out.reserve((n / 4) * 3);

    for (size_t i = 0; i < n; i += 4) {
        const int8_t c0 = b64_decode_char(static_cast<uint8_t>(in[i + 0]));
        const int8_t c1 = b64_decode_char(static_cast<uint8_t>(in[i + 1]));
        // '=' padding: treat as 0 for bit extraction; byte is not emitted.
        const bool pad2 = (in[i + 2] == '=');
        const bool pad3 = (in[i + 3] == '=');
        const int8_t c2 = pad2 ? 0 : b64_decode_char(static_cast<uint8_t>(in[i + 2]));
        const int8_t c3 = pad3 ? 0 : b64_decode_char(static_cast<uint8_t>(in[i + 3]));

        // Invalid padding: '=' at position 2 requires '=' at position 3.
        if (pad2 && !pad3) {
            out.clear();
            return out;
        }

        // Any invalid (non-alphabet, non-padding) character → return empty.
        if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) {
            out.clear();
            return out;
        }

        const uint32_t triple =
            (static_cast<uint32_t>(c0) << 18) |
            (static_cast<uint32_t>(c1) << 12) |
            (static_cast<uint32_t>(c2) <<  6) |
             static_cast<uint32_t>(c3);

        out.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));
        if (!pad2) out.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));
        if (!pad3) out.push_back(static_cast<uint8_t>( triple       & 0xFF));
    }
    return out;
}
