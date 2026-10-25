#ifndef WAYSTONE_VERSION_H
#define WAYSTONE_VERSION_H

// Value comes from the repo-root VERSION file via -DWS_APP_VERSION (3ds/Makefile).
#ifndef WS_APP_VERSION
#error "WS_APP_VERSION undefined: build through 3ds/Makefile, which reads ../VERSION"
#endif

#endif
