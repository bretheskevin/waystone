#ifndef WAYSTONE_BASE64_H
#define WAYSTONE_BASE64_H

#include <cstddef>
#include <cstdint>
#include <string>

// Encode binary data to standard base64 (RFC 4648, alphabet A-Z a-z 0-9 +/ with = padding).
std::string base64_encode(const uint8_t* data, size_t len);

#endif
