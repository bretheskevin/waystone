#ifndef WAYSTONE_LOG_ROTATE_H
#define WAYSTONE_LOG_ROTATE_H

// Upper bound for a live log file. When log.txt exceeds this at startup it is
// rotated to log.txt.old (previous rotation discarded), so at most ~2x this
// many bytes of logs are kept on the SD card.
#define WS_LOG_MAX_BYTES (256L * 1024L)

// If the file at `path` is larger than max_bytes, rename it to "<path>.old"
// (overwriting any previous rotation). If the rename fails, the file is
// deleted instead so growth stays bounded either way. Missing file = no-op.
void log_cap_rotate(const char* path, long max_bytes);

#endif
