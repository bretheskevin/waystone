#ifndef WAYSTONE_NET_H
#define WAYSTONE_NET_H

// PUT a probe blob, GET it back and compare, PROPFIND Depth:1.
// Prints HTTP status codes and response bodies to the libnx console.
// Returns 0 on all-expected, nonzero on any failure.
int net_webdav_probe(const char* base_url, const char* user, const char* pass);

#endif
