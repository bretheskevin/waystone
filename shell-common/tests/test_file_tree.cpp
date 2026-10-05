// Host-only test for the shared WsFileTree / WsSaveList codec.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra \
//     -I shell-common \
//     shell-common/tests/test_file_tree.cpp shell-common/file_tree.cpp \
//     -o /tmp/test_file_tree && /tmp/test_file_tree
#include "file_tree.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// Frozen golden — MUST match ffi/src/wire.rs FILE_TREE_GOLDEN exactly.
static const uint8_t FILE_TREE_GOLDEN[] = {
    0x02,0x00,0x00,0x00,
    0x01,0x00,0x00,0x00, 0x61, 0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0xAA,
    0x02,0x00,0x00,0x00, 0x62,0x62, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
static const uint8_t SAVE_LIST_GOLDEN[] = {
    0x01,0x00,0x00,0x00,
    0x02,0x00,0x00,0x00, 0x7B,0x7D,
    0x00,0x00,0x00,0x00
};

static void test_encode_golden() {
    std::vector<FileTreeEntry> files;
    files.push_back(FileTreeEntry("a", std::vector<uint8_t>(1, 0xAA)));
    files.push_back(FileTreeEntry("bb", std::vector<uint8_t>()));
    std::vector<uint8_t> out = file_tree_encode(files);
    assert(out.size() == sizeof(FILE_TREE_GOLDEN));
    assert(memcmp(out.data(), FILE_TREE_GOLDEN, sizeof(FILE_TREE_GOLDEN)) == 0);
    printf("test_encode_golden PASSED\n");
}

static void test_decode_golden() {
    std::vector<FileTreeEntry> out;
    assert(file_tree_decode(FILE_TREE_GOLDEN, sizeof(FILE_TREE_GOLDEN), &out));
    assert(out.size() == 2);
    assert(out[0].first == "a" && out[0].second.size() == 1 && out[0].second[0] == 0xAA);
    assert(out[1].first == "bb" && out[1].second.empty());
    printf("test_decode_golden PASSED\n");
}

static void test_round_trip() {
    std::vector<FileTreeEntry> files;
    std::vector<uint8_t> d; d.push_back(0xDE); d.push_back(0xAD);
    files.push_back(FileTreeEntry("saves/main.sav", d));
    files.push_back(FileTreeEntry("empty", std::vector<uint8_t>()));
    std::vector<uint8_t> buf = file_tree_encode(files);
    std::vector<FileTreeEntry> out;
    assert(file_tree_decode(buf.data(), buf.size(), &out));
    assert(out == files);
    printf("test_round_trip PASSED\n");
}

static void test_decode_rejects_truncation() {
    std::vector<FileTreeEntry> out;
    const uint8_t claims_one[] = {0x01,0x00,0x00,0x00}; // 1 entry, no body
    assert(!file_tree_decode(claims_one, sizeof(claims_one), &out));
    assert(out.empty());
    const uint8_t short_count[] = {0x00,0x00};
    assert(!file_tree_decode(short_count, sizeof(short_count), &out));
    printf("test_decode_rejects_truncation PASSED\n");
}

static void test_decode_rejects_overflow() {
    std::vector<FileTreeEntry> out;
    // count=1, path_len=0, data_len=UINT64_MAX
    const uint8_t buf[] = {
        0x01,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
    };
    assert(!file_tree_decode(buf, sizeof(buf), &out));
    printf("test_decode_rejects_overflow PASSED\n");
}

static void test_decode_rejects_huge_count() {
    std::vector<FileTreeEntry> out;
    const uint8_t buf[] = {0xFF,0xFF,0xFF,0xFF};
    assert(!file_tree_decode(buf, sizeof(buf), &out));
    printf("test_decode_rejects_huge_count PASSED\n");
}

static void test_save_list_golden_and_decode() {
    std::vector<SaveListEntry> out;
    assert(save_list_decode(SAVE_LIST_GOLDEN, sizeof(SAVE_LIST_GOLDEN), &out));
    assert(out.size() == 1);
    assert(out[0].meta_json == "{}");
    std::vector<FileTreeEntry> files;
    assert(file_tree_decode(out[0].files_ptr, out[0].files_len, &files));
    assert(files.empty());
    printf("test_save_list_golden_and_decode PASSED\n");
}

static void test_save_list_rejects_truncation() {
    std::vector<SaveListEntry> out;
    assert(!save_list_decode(SAVE_LIST_GOLDEN, sizeof(SAVE_LIST_GOLDEN) - 1, &out));
    assert(out.empty());
    printf("test_save_list_rejects_truncation PASSED\n");
}

int main() {
    test_encode_golden();
    test_decode_golden();
    test_round_trip();
    test_decode_rejects_truncation();
    test_decode_rejects_overflow();
    test_decode_rejects_huge_count();
    test_save_list_golden_and_decode();
    test_save_list_rejects_truncation();
    printf("ALL file_tree tests PASSED\n");
    return 0;
}
