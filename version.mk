# Shared app version for the console Makefiles: the repo-root VERSION file is the single source of truth.
WS_VERSION_FILE := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))VERSION)
WS_VERSION := $(strip $(shell cat $(WS_VERSION_FILE) 2>/dev/null))
ifeq ($(WS_VERSION),)
$(error VERSION file missing or empty: $(WS_VERSION_FILE))
endif
WS_VERSION_DEFINE := -DWS_APP_VERSION=\"$(WS_VERSION)\"
