#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/devkitARM/3ds_rules

#---------------------------------------------------------------------------------
# App metadata (feeds smdhtool for .3dsx SMDH + makerom/bannertool for .cia)
#---------------------------------------------------------------------------------
APP_TITLE       := Waystone
APP_DESCRIPTION := Cross-platform game-save sync
APP_AUTHOR      := kandid
APP_ICON        := $(TOPDIR)/app/icon.png

#---------------------------------------------------------------------------------
TARGET		:=	waystone-3ds-spike
BUILD		:=	build
# Build-mode guard.
#   Default (no CONSOLE): GUI build (citro2d). Entry: source/app_main.cpp. main.cpp excluded.
#   CONSOLE=1: console driver. Entry: source/main.cpp. app_main.cpp + source/ui/* excluded.
ifeq ($(CONSOLE),1)
SOURCES  := source ../shell-common
EXCLUDES := source/app_main.cpp ../shell-common/test_wsconfig.cpp
LIBS     := -lwaystone_ffi -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru -lm
else
SOURCES  := source source/ui ../shell-common
EXCLUDES := source/main.cpp ../shell-common/test_wsconfig.cpp
LIBS     := -lwaystone_ffi -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lcitro2d -lcitro3d -lctru -lm
endif
DATA		:=	data
INCLUDES	:=	../ffi/include source source/ui ../shell-common
ROMFS		:=	romfs

# WAYSTONE_TARGET selects which cross-compiled libwaystone_ffi.a to link.
# build.sh overrides it to armv6k-none-eabi for the custom-target fallback.
WAYSTONE_TARGET	?=	armv6k-nintendo-3ds

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections -fdata-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) -D__3DS__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=c++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(CTRULIB)

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
CPPFILES	:=	$(filter-out $(notdir $(EXCLUDES)),$(CPPFILES))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

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

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

# Rust staticlib dir first, then the standard devkitPro lib dirs.
export LIBPATHS	:=	-L$(CURDIR)/../target/$(WAYSTONE_TARGET)/release \
			$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifneq ($(strip $(ROMFS)),)
	export _3DSXFLAGS += --romfs=$(TOPDIR)/$(ROMFS) --smdh=$(OUTPUT).smdh
endif

.PHONY: $(BUILD) clean all cia

#---------------------------------------------------------------------------------
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@[ -d $@ ] || mkdir -p $@

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh $(TARGET).cia $(TOPDIR)/app.bnr

#---------------------------------------------------------------------------------
# CIA packaging (post-link). Requires bannertool + makerom on PATH.
# Usage: make -C 3ds cia   —  Title ID 0x000400000FF3FF00 (UniqueId 0xFF3FF) — NEVER change.
#---------------------------------------------------------------------------------
CIA_VERSION_MAJOR := $(shell sed -n 's/.*WS_APP_VERSION "\([0-9]*\)\..*/\1/p' $(TOPDIR)/source/version.h)
CIA_VERSION_MINOR := $(shell sed -n 's/.*WS_APP_VERSION "[0-9]*\.\([0-9]*\)\..*/\1/p' $(TOPDIR)/source/version.h)
CIA_VERSION_MICRO := $(shell sed -n 's/.*WS_APP_VERSION "[0-9]*\.[0-9]*\.\([0-9]*\)".*/\1/p' $(TOPDIR)/source/version.h)

cia: all
	@echo "[cia] building banner ..."
	bannertool makebanner -i $(TOPDIR)/app/banner.png -a $(TOPDIR)/app/banner.wav -o $(TOPDIR)/app.bnr
	@echo "[cia] building CIA (title ID 0x000400000FF3FF00) ..."
	makerom -f cia -o $(TARGET).cia \
		-elf $(TARGET).elf -rsf $(TOPDIR)/app.rsf \
		-icon $(TARGET).smdh -banner $(TOPDIR)/app.bnr \
		-major $(CIA_VERSION_MAJOR) -minor $(CIA_VERSION_MINOR) -micro $(CIA_VERSION_MICRO)
	@echo "[cia] done -> $(TARGET).cia"

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
all	:	$(OUTPUT).3dsx

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).elf	:	$(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
