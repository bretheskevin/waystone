#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
TARGET		:=	waystone-spike
BUILD		:=	build
DATA		:=	data
INCLUDES	:=	../ffi/include source ../shell-common
ROMFS		:=	romfs

# WAYSTONE_TARGET selects which cross-compiled libwaystone_ffi.a to link.
# build.sh overrides it to aarch64-none-elf for the custom-target fallback.
WAYSTONE_TARGET	?=	aarch64-nintendo-switch-freestanding

#---------------------------------------------------------------------------------
# Build-mode guard.
#
#   Default (no CONSOLE):  GUI build — borealis deko3d backend.
#     Entry point: source/app_main.cpp.  source/main.cpp excluded.
#   CONSOLE=1:             Console driver build — no borealis.
#     Entry point: source/main.cpp.  source/app_main.cpp excluded.
#
ifeq ($(CONSOLE),1)
SOURCES		:=	source ../shell-common
EXCLUDES	:=	source/app_main.cpp ../shell-common/test_wsconfig.cpp
else
SOURCES		:=	source source/ui ../shell-common
EXCLUDES	:=	source/main.cpp ../shell-common/test_wsconfig.cpp
ROMFS_ASSETS	:=	romfs-assets
BOREALIS_PATCH	:=	borealis-patch
# Path to the borealis repo root (relative to TOPDIR = switch/).
BOREALIS_PATH	:=	lib/borealis
# Compiled deko3d shaders will be written to $(ROMFS)/$(OUT_SHADERS)/ at build time.
OUT_SHADERS	:=	shaders
# Borealis romfs resources (fonts/i18n/img/xml) are assembled from the submodule at
# build time. switch/romfs/{i18n,img,...} are NOT committed; submodule is the single source.
BOREALIS_RESOURCES	:=	$(BOREALIS_PATH)/resources
# borealis TUs we intentionally drop from this slice.
# swkbd.cpp calls swkbdConfigSetStringLenMaxExt which was removed from current libnx.
# On-screen keyboard is deferred; no other borealis TU references Swkbd, so dropping
# this TU is link-safe.
BOREALIS_EXCLUDES	:=	swkbd.cpp
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__SWITCH__

ifeq ($(CONSOLE),1)
# Console build: apply globally — no borealis TUs in the tree.
CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions
else
# GUI build: omit -fno-rtti/-fno-exceptions globally so borealis TUs can compile.
# -include flags force-prepend std headers missing from nanovg-deko3d transitive includes.
# borealis view.hpp has typeid() inline — RTTI stays on for TUs including <borealis.hpp>.
# Engine TUs (saves/sync/net/json/base64) re-add -fno-rtti/-fno-exceptions per-target below.
CXXFLAGS	:= $(CFLAGS) -std=c++17 -Wno-volatile -include optional -include variant -include string_view
endif

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lwaystone_ffi $(shell $(DEVKITPRO)/portlibs/switch/bin/curl-config --libs) -lnx

#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)

# Include borealis Makefile fragment for the GUI build.
# Appends to SOURCES, INCLUDES, LIBS, CXXFLAGS (-DYG_ENABLE_EVENTS, -fdata-sections,
# -DBRLS_RESOURCES="romfs:/"), and links -ldeko3d -lm.
# deko3d (including uam shader compiler and libdeko3d.a) ships in the devkita64 base image.
# Pinned submodule: switch/lib/borealis @ 20e2d33b6c4ffce139ce304c503c04f5b94da920
ifneq ($(CONSOLE),1)
include $(TOPDIR)/$(BOREALIS_PATH)/library/borealis.mk
endif

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
GLSLFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.glsl)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

# Exclude the inactive entry point (main.cpp or app_main.cpp) and any borealis TUs
# that are intentionally dropped for this slice (swkbd.cpp in GUI mode).
# BOREALIS_EXCLUDES is empty/unset in CONSOLE=1 mode, so it is a no-op there.
CPPFILES	:=	$(filter-out $(notdir $(EXCLUDES)) $(BOREALIS_EXCLUDES),$(CPPFILES))

#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

ifneq ($(strip $(ROMFS)),)
	ROMFS_TARGETS	:=
	ROMFS_FOLDERS	:=
	ifneq ($(strip $(OUT_SHADERS)),)
		ROMFS_SHADERS	:= $(ROMFS)/$(OUT_SHADERS)
		ROMFS_TARGETS	+= $(patsubst %.glsl,$(ROMFS_SHADERS)/%.dksh,$(GLSLFILES))
		ROMFS_FOLDERS	+= $(ROMFS_SHADERS)
	endif

	export ROMFS_DEPS := $(foreach file,$(ROMFS_TARGETS),$(CURDIR)/$(file))
endif

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

# Rust staticlib dir first, then the standard devkitPro lib dirs.
export LIBPATHS	:=	-L$(CURDIR)/../target/$(WAYSTONE_TARGET)/release \
			$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(strip $(ROMFS)),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all romfs-assets $(BOREALIS_PATCH)

#---------------------------------------------------------------------------------
# romfs-assets is a prereq only in GUI mode; in CONSOLE=1 the var expands to empty.
all: $(ROMFS_ASSETS) $(ROMFS_TARGETS) $(BOREALIS_PATCH) | $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@[ -d $@ ] || mkdir -p $@

# Copy borealis fonts/i18n/img/xml from submodule into romfs before packaging (GUI only).
# Idempotent: cp -R src/. dest/ overwrites in place on repeated builds.
# Remove the .gitignore that cp -R brings in so it never lands in the embedded romfs.
ifneq ($(CONSOLE),1)
romfs-assets:
	@cp -R $(BOREALIS_RESOURCES)/. $(ROMFS)/
	@rm -f $(ROMFS)/.gitignore

# Apply tint patch to borealis before any borealis TU is compiled.
# Idempotent: skips if already applied; fails loudly when patch is incompatible.
# operator[] override replaces insert so apply_waystone_tint() overrides take effect.
borealis-patch:
	@if git -C lib/borealis apply --check $(TOPDIR)/patches/borealis-theme-tint.patch 2>/dev/null; then \
	  git -C lib/borealis apply $(TOPDIR)/patches/borealis-theme-tint.patch; \
	elif ! git -C lib/borealis apply --check --reverse $(TOPDIR)/patches/borealis-theme-tint.patch 2>/dev/null; then \
	  echo "ERROR: patches/borealis-theme-tint.patch cannot apply -- rebase on current borealis" >&2; \
	  exit 1; \
	fi
endif

# deko3d shader compilation: .glsl → .dksh via uam (present in devkita64 base image).
# ROMFS_TARGETS is empty in CONSOLE=1 mode (no OUT_SHADERS, no .glsl in SOURCES).
ifneq ($(strip $(ROMFS_TARGETS)),)

$(ROMFS_TARGETS): | $(ROMFS_FOLDERS)

$(ROMFS_FOLDERS):
	@mkdir -p $@

$(ROMFS_SHADERS)/%_vsh.dksh: %_vsh.glsl
	@echo {vert} $(notdir $<)
	@uam -s vert -o $@ $<

$(ROMFS_SHADERS)/%_fsh.dksh: %_fsh.glsl
	@echo {frag} $(notdir $<)
	@uam -s frag -o $@ $<

$(ROMFS_SHADERS)/%.dksh: %.glsl
	@echo {comp} $(notdir $<)
	@uam -s comp -o $@ $<

endif

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf \
		$(filter-out $(ROMFS)/cacert.pem,$(wildcard $(ROMFS)/*)) \
		$(wildcard $(ROMFS)/.gitignore)

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

# GUI build: re-apply -fno-rtti and -fno-exceptions only to our own engine TUs.
# These files do NOT include borealis headers, so RTTI/exceptions are not needed.
# app_main.cpp includes <borealis.hpp> which has typeid() inline in view.hpp —
# it must NOT have -fno-rtti; it inherits global CXXFLAGS (RTTI enabled).
# CONSOLE=1: flags are already in global CXXFLAGS; this block is skipped.
ifneq ($(CONSOLE),1)
OUR_ENGINE_OBJS := saves.o sync.o net.o json.o base64.o snapshot.o wsconfig.o
$(OUR_ENGINE_OBJS): CXXFLAGS += -fno-rtti -fno-exceptions
endif

#---------------------------------------------------------------------------------
all	:	$(OUTPUT).nro

# NRO depends on ROMFS_DEPS (compiled .dksh shaders) so the build fails loudly
# if shaders were not compiled. ROMFS_DEPS is empty in CONSOLE=1 mode.
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp $(ROMFS_DEPS)

$(OUTPUT).elf	:	$(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
