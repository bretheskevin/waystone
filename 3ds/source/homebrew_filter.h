#ifndef WAYSTONE_HOMEBREW_FILTER_H
#define WAYSTONE_HOMEBREW_FILTER_H

#include <cstdint>

namespace homebrew {

// Fetch (or load from SD cache) the set of homebrew title ids from Universal-DB.
// Idempotent: runs the fetch once per process, subsequent calls return immediately.
// Fail-open: on any fetch/parse/cache failure the set is left empty (all titles shown).
void ensure_loaded();

// Returns true if tid is a known homebrew title id (from Universal-DB).
bool is_homebrew(uint64_t tid);

} // namespace homebrew

#endif
