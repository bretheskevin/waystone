#ifndef WAYSTONE_BASE64_H
#define WAYSTONE_BASE64_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Encode binary data to standard base64 (RFC 4648, alphabet A-Z a-z 0-9 +/ with = padding).
std::string base64_encode(const uint8_t* data, size_t len);

// Decode standard base64 (RFC 4648 STANDARD: A-Z a-z 0-9 +/ with = padding).
// Returns empty vector on invalid input (no exceptions, no RTTI).
std::vector<uint8_t> base64_decode(const std::string& in);

#endif
