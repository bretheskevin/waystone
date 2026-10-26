#include "registry.h"
#include <cstdio>
#include <map>

// Function-local static: safe to use from other TUs' static initializers.
static std::map<std::string, PreviewModeFn>& modes() {
    static std::map<std::string, PreviewModeFn> m;
    return m;
}

void preview_register(const char* name, PreviewModeFn fn) {
    if (!modes().emplace(name, fn).second)
        fprintf(stderr, "[ui] preview mode '%s' registered twice; keeping the first\n", name);
}

PreviewModeFn preview_find(const char* name) {
    auto it = modes().find(name);
    return it == modes().end() ? nullptr : it->second;
}

std::vector<std::string> preview_mode_names() {
    std::vector<std::string> out;
    for (const auto& kv : modes()) out.push_back(kv.first);
    return out;
}
