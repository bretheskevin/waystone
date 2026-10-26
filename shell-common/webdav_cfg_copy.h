#ifndef WAYSTONE_WEBDAV_CFG_COPY_H
#define WAYSTONE_WEBDAV_CFG_COPY_H

#include <string>
#include "net.h"
#include "secure_clear.h"

// Owned copy of a borrowed WebDavCfg. The previous password is zeroized on re-assign and on
// destruction; non-copyable so the secret is never duplicated. Null source fields become "".
class WebDavCfgCopy {
public:
    WebDavCfgCopy() {}
    ~WebDavCfgCopy() { clear_pass(); }
    WebDavCfgCopy(const WebDavCfgCopy&) = delete;
    WebDavCfgCopy& operator=(const WebDavCfgCopy&) = delete;

    void assign(const WebDavCfg& src) {
        clear_pass();
        url_ = src.base_url ? src.base_url : "";
        user_ = src.user ? src.user : "";
        pass_ = src.pass ? src.pass : "";
    }

    WebDavCfg cfg() const {
        WebDavCfg c;
        c.base_url = url_.c_str();
        c.user = user_.c_str();
        c.pass = pass_.c_str();
        return c;
    }

private:
    void clear_pass() {
        if (!pass_.empty()) {
            secure_clear(&pass_[0], pass_.size());
            pass_.clear();
        }
    }

    std::string url_, user_, pass_;
};

#endif
