#---------------------------------------------------------------------------------
# ryu_ldn_nx Sysmodule Makefile
# Based on Atmosphere stratosphere sysmodule template
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
# pull in common stratosphere sysmodule configuration
#---------------------------------------------------------------------------------
include $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/Atmosphere-libs/config/templates/stratosphere.mk

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#---------------------------------------------------------------------------------
TARGET		:=	ryu_ldn_nx
BUILD		:=	build
SOURCES		:=	source source/config source/debug source/network source/protocol source/ldn source/bsd source/p2p
DATA		:=	data
INCLUDES	:=	source

#---------------------------------------------------------------------------------
# version control constants
#---------------------------------------------------------------------------------
TARGET_VERSION := 0.1.0

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
VERSION_DEFINES	:= -DRYU_LDN_VERSION="\"${TARGET_VERSION}\""

CFLAGS		+= $(VERSION_DEFINES)
CXXFLAGS	+= $(VERSION_DEFINES)

#---------------------------------------------------------------------------------
# Compilation hardening (H4) + warning policy (DEV-H8)
#---------------------------------------------------------------------------------
# -fstack-protector-strong : stack canaries on functions with non-trivial
#   buffers. RISK (N4, not verified in this gate): under devkitPro/newlib
#   (aarch64-none-elf toolchain + libnx runtime), the canary symbol
#   __stack_chk_guard / __stack_chk_fail must be provided by the runtime.
#   The pre-built libstratosphere.a in the GHCR image has NOT been build-
#   verified with this flag in this session — the build was not re-run
#   here. If `docker compose run --rm build` produces a missing-symbol
#   linker error on __stack_chk_*, drop this flag (the runtime still
#   gets RELRO + NX + PIE from the inherited stratosphere.mk SETTINGS).
#   Action: re-run `docker compose run --rm build` after this change to
#   confirm linkage before merging.
# -D_FORTIFY_SOURCE=2      : REMOVED — newlib (devkitPro) has partial
#   _FORTIFY_SOURCE support that emits calls to __*_chk runtime functions
#   (__fprintf_chk, __sprintf_chk, __memcpy_chk, etc.). These fortified
#   wrappers call abort() when __builtin_object_size returns 0 or (size_t)-1
#   for heap/stack buffers whose size the compiler cannot statically determine
#   — a common case in sysmodule code that uses void* allocators and fixed
#   char arrays. The runtime abort manifests as std::abort (0xFFE) at boot
#   during the first fprintf/fopen call in InitializeSystemModule().
#   Upstream Atmosphere-libs/stratosphere.mk does NOT emit _FORTIFY_SOURCE;
#   the flag was a project-local addition, not inherited. Same class of
#   problem as -fstack-protector-strong (missing/untested runtime support).
# -Wall -Wextra -Werror     : strict warnings promoted to errors
#   NOTE: -Wextra and -Werror are already inherited from stratosphere.mk
#   (SETTINGS), but we re-emit them at project level for explicit intent and
#   to keep the policy visible without reading the submodule.
# -Wl,-z,relro -Wl,-z,now    : full RELRO; also inherited from stratosphere.mk
#   LDFLAGS, re-emitted here for project-level visibility (duplicate linker
#   flags are deduplicated by the linker — harmless). The two flags are
#   emitted as separate -Wl, tokens so each RELRO/now option reaches the
#   linker as its own argument (combining them in a single -Wl, requires the
#   linker to parse comma-separated sub-options, which works with GNU ld but
#   is fragile across toolchains — devkitPro uses arm-none-elf-g++ which
#   inherits binutils behavior and historically rejects the combined form
#   in some versions).
# -Wno-missing-field-initializers : benign; aggregates intentionally use
#   partial initialization (e.g. NetworkInfo nodes). Already set in
#   stratosphere.mk SETTINGS; re-emitted here so the project keeps building
#   even if a future Atmosphere-libs bump drops it from the submodule.
# -Wno-unused-result       : see UNUSED_RESULT_WARNING below. Suppressed
#   globally (rather than per-call via (void) casts) because the ignored
#   returns are spread across libnx/stratosphere wrappers and the project's
#   own Result types: a top-level check on a Result-ifying wrapper already
#   covers correctness, while the underlying libc-style returns
#   (e.g. ::write, ::read on BSD sockets) have their failure paths handled
#   by the surrounding SocketResult state machine. A targeted suppression
#   would require touching every call site and would not be less risky than
#   the global flag — the global flag is the documented trade-off.
#---------------------------------------------------------------------------------
HARDENING_FLAGS	:=
WARN_FLAGS		:= -Wall -Wextra -Werror \
				   -Wno-missing-field-initializers \
				   -Wno-unused-result
# Two separate -Wl, tokens so each linker option stands on its own
# (see header comment for the rationale — combined form is fragile).
LINK_HARDENING	:= -Wl,-z,relro -Wl,-z,now

CFLAGS		+= $(HARDENING_FLAGS) $(WARN_FLAGS)
CXXFLAGS	+= $(HARDENING_FLAGS) $(WARN_FLAGS)
LDFLAGS		+= $(LINK_HARDENING)

#---------------------------------------------------------------------------------
# Additional libraries for P2P/UPnP support
# switch-miniupnpc is provided by devkitPro
#---------------------------------------------------------------------------------
LIBS		+= -lminiupnpc

#---------------------------------------------------------------------------------
# Resolver wrappers — sfdnsres-backed getaddrinfo()/freeaddrinfo()/getnameinfo()
# from libnx DABRT in our boot2 sysmodule context. We redirect every call
# (including those baked into libminiupnpc.a) to inet_pton-based stubs in
# source/network/dns_wrap.cpp; UPnP only needs IPv4-literal resolution.
#---------------------------------------------------------------------------------
CXXWRAPS	+= -Wl,--wrap,getaddrinfo \
		   -Wl,--wrap,freeaddrinfo \
		   -Wl,--wrap,getnameinfo \
		   -Wl,--wrap,gai_strerror

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES      :=	$(foreach dir,$(SOURCES),$(filter-out $(notdir $(wildcard $(dir)/*.arch.*.c)) $(notdir $(wildcard $(dir)/*.board.*.c)) $(notdir $(wildcard $(dir)/*.os.*.c)), \
                                                      $(notdir $(wildcard $(dir)/*.c))))
CFILES      +=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.arch.$(ATMOSPHERE_ARCH_NAME).c)))
CFILES      +=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.board.$(ATMOSPHERE_BOARD_NAME).c)))
CFILES      +=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.os.$(ATMOSPHERE_OS_NAME).c)))

CPPFILES    :=	$(foreach dir,$(SOURCES),$(filter-out $(notdir $(wildcard $(dir)/*.arch.*.cpp)) $(notdir $(wildcard $(dir)/*.board.*.cpp)) $(notdir $(wildcard $(dir)/*.os.*.cpp)), \
                                                      $(notdir $(wildcard $(dir)/*.cpp))))
CPPFILES    +=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.arch.$(ATMOSPHERE_ARCH_NAME).cpp)))
CPPFILES    +=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.board.$(ATMOSPHERE_BOARD_NAME).cpp)))
CPPFILES    +=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.os.$(ATMOSPHERE_OS_NAME).cpp)))

SFILES      :=	$(foreach dir,$(SOURCES),$(filter-out $(notdir $(wildcard $(dir)/*.arch.*.s)) $(notdir $(wildcard $(dir)/*.board.*.s)) $(notdir $(wildcard $(dir)/*.os.*.s)), \
                                                      $(notdir $(wildcard $(dir)/*.s))))
SFILES      +=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.arch.$(ATMOSPHERE_ARCH_NAME).s)))
SFILES      +=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.board.$(ATMOSPHERE_BOARD_NAME).s)))
SFILES      +=  $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.os.$(ATMOSPHERE_OS_NAME).s)))

BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
			$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			$(foreach dir,$(AMS_LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					$(foreach dir,$(AMS_LIBDIRS),-L$(dir)/lib) \
					$(foreach dir,$(AMS_LIBDIRS),-L$(dir)/$(ATMOSPHERE_LIBRARY_DIR))

export BUILD_EXEFS_SRC := $(TOPDIR)/$(EXEFS_SRC)

export APP_JSON := $(TOPDIR)/res/app.json

.PHONY: $(BUILD) clean all rebuild dist

#---------------------------------------------------------------------------------
# Title ID for sysmodule
#---------------------------------------------------------------------------------
TITLE_ID	:=	4200000000000010

#---------------------------------------------------------------------------------
# OUTPUT_DIR: complete SD card image at project root
#---------------------------------------------------------------------------------
OUTPUT_DIR	:=	$(CURDIR)/../output

#---------------------------------------------------------------------------------
all: $(BUILD)

#---------------------------------------------------------------------------------
# dist: create complete SD card structure + ZIP
# Produces output/ with sysmodule, overlay, config, gamelist
#---------------------------------------------------------------------------------
dist: all
	@echo "Creating SD card structure..."
	@rm -rf $(OUTPUT_DIR)
	@mkdir -p $(OUTPUT_DIR)/atmosphere/contents/$(TITLE_ID)/flags
	@mkdir -p $(OUTPUT_DIR)/switch/.overlays
	@mkdir -p $(OUTPUT_DIR)/config/ryu_ldn_nx
	@cp $(TARGET).nsp $(OUTPUT_DIR)/atmosphere/contents/$(TITLE_ID)/exefs.nsp
	@cp $(TARGET).npdm $(OUTPUT_DIR)/atmosphere/contents/$(TITLE_ID)/main.npdm
	@cp res/toolbox.json $(OUTPUT_DIR)/atmosphere/contents/$(TITLE_ID)/
	@touch $(OUTPUT_DIR)/atmosphere/contents/$(TITLE_ID)/flags/boot2.flag
	@if [ -f $(CURDIR)/../overlay/ryu_ldn_nx_overlay.ovl ]; then \
		cp $(CURDIR)/../overlay/ryu_ldn_nx_overlay.ovl $(OUTPUT_DIR)/switch/.overlays/; \
		echo "Included overlay"; \
	else \
		echo "WARNING: overlay not found, build overlay first"; \
	fi
	@cp $(CURDIR)/../config/ryu_ldn_nx/config.ini.example $(OUTPUT_DIR)/config/ryu_ldn_nx/config.ini.example
	@cp $(CURDIR)/../config/ryu_ldn_nx/gamelist.txt $(OUTPUT_DIR)/config/ryu_ldn_nx/
	@echo "Included gamelist.txt and config.ini.example"
	@echo "Creating ZIP..."
	@cd $(OUTPUT_DIR) && zip -r ../$(TARGET)-release.zip .
	@echo "Created $(TARGET)-release.zip"
	@echo "SD card structure in output/"

#---------------------------------------------------------------------------------
rebuild: clean all

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).kip $(TARGET).elf $(TARGET).nso $(TARGET).npdm $(TARGET).nsp $(OUTPUT_DIR) $(TARGET)-release.zip

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all: $(OUTPUT).nsp

$(OUTPUT).kip :	$(OUTPUT).elf
$(OUTPUT).nsp :	$(OUTPUT).nso $(OUTPUT).npdm
$(OUTPUT).nso :	$(OUTPUT).elf

$(OUTPUT).elf :	$(OFILES)

$(OFILES) : $(ATMOSPHERE_LIBRARIES_DIR)/libstratosphere/$(ATMOSPHERE_LIBRARY_DIR)/libstratosphere.a

%.npdm  :   $(APP_JSON)
	@echo built ... $< $@
	@npdmtool $< $@
	@echo built ... $(notdir $@)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
