#include "session_store.h"

void session_store_set_device_key_fn(DeviceKeyFn) {}
bool session_store_save(const uint8_t*, size_t, const std::string&) { return true; }
bool session_store_load(uint8_t*, size_t, std::string&) { return false; }
bool session_store_exists() { return false; }
void session_store_clear() {}
void persist_session(WsVault*, const std::string&) {}
