// Host-only test for WebDavCfgCopy (owned WebDAV url/user/pass backing a WebDavCfg).
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common \
//     shell-common/tests/test_webdav_cfg_copy.cpp -o /tmp/test_webdav_cfg_copy && /tmp/test_webdav_cfg_copy
#include "webdav_cfg_copy.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static void test_default_is_empty() {
    WebDavCfgCopy c;
    WebDavCfg cfg = c.cfg();
    assert(cfg.base_url && std::strcmp(cfg.base_url, "") == 0);
    assert(cfg.user && std::strcmp(cfg.user, "") == 0);
    assert(cfg.pass && std::strcmp(cfg.pass, "") == 0);
}

static void test_assign_survives_source_change() {
    std::string* url = new std::string("https://host:5000");
    std::string* user = new std::string("alice");
    std::string* pass = new std::string("s3cret");
    WebDavCfg src = {url->c_str(), user->c_str(), pass->c_str()};
    WebDavCfgCopy c;
    c.assign(src);
    (*url)[0] = 'X';
    (*user)[0] = 'X';
    (*pass)[0] = 'X';
    delete url;
    delete user;
    delete pass;
    WebDavCfg cfg = c.cfg();
    assert(std::strcmp(cfg.base_url, "https://host:5000") == 0);
    assert(std::strcmp(cfg.user, "alice") == 0);
    assert(std::strcmp(cfg.pass, "s3cret") == 0);
}

static void test_reassign_reflects_new_values() {
    WebDavCfg a = {"http://old", "olduser", "oldpass"};
    WebDavCfg b = {"https://new.example/dav", "newuser", "a-much-longer-new-password"};
    WebDavCfgCopy c;
    c.assign(a);
    c.assign(b);
    WebDavCfg cfg = c.cfg();
    assert(std::strcmp(cfg.base_url, "https://new.example/dav") == 0);
    assert(std::strcmp(cfg.user, "newuser") == 0);
    assert(std::strcmp(cfg.pass, "a-much-longer-new-password") == 0);
    c.assign(a);
    cfg = c.cfg();
    assert(std::strcmp(cfg.base_url, "http://old") == 0);
    assert(std::strcmp(cfg.pass, "oldpass") == 0);
}

static void test_null_fields_become_empty() {
    WebDavCfg src = {nullptr, nullptr, nullptr};
    WebDavCfgCopy c;
    c.assign(WebDavCfg{"u", "v", "w"});
    c.assign(src);
    WebDavCfg cfg = c.cfg();
    assert(cfg.base_url && cfg.base_url[0] == '\0');
    assert(cfg.user && cfg.user[0] == '\0');
    assert(cfg.pass && cfg.pass[0] == '\0');
}

int main() {
    test_default_is_empty();
    test_assign_survives_source_change();
    test_reassign_reflects_new_values();
    test_null_fields_become_empty();
    printf("test_webdav_cfg_copy: all passed\n");
    return 0;
}
