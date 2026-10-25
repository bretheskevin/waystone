#ifndef WAYSTONE_SWITCH_VERSION_H
#define WAYSTONE_SWITCH_VERSION_H

// Value comes from the repo-root VERSION file via -DWS_APP_VERSION (switch/Makefile, preview script).
#ifndef WS_APP_VERSION
#error "WS_APP_VERSION undefined: build through switch/Makefile, which reads ../VERSION"
#endif

#endif
