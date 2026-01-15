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
SOURCES		:=	source source/config source/debug source/network source/protocol source/ldn source/bsd
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
all: $(BUILD)

#---------------------------------------------------------------------------------
# dist: create SD card structure ZIP
#---------------------------------------------------------------------------------
dist: all
	@echo "Creating SD card structure..."
	@rm -rf dist
	@mkdir -p dist/atmosphere/contents/$(TITLE_ID)/flags
	@cp $(TARGET).nsp dist/atmosphere/contents/$(TITLE_ID)/exefs.nsp 2>/dev/null || true
	@cp $(TARGET).npdm dist/atmosphere/contents/$(TITLE_ID)/main.npdm 2>/dev/null || true
	@cp res/toolbox.json dist/atmosphere/contents/$(TITLE_ID)/ 2>/dev/null || true
	@touch dist/atmosphere/contents/$(TITLE_ID)/flags/boot2.flag
	@echo "Creating ZIP..."
	@cd dist && zip -r ../$(TARGET)-sysmodule.zip .
	@echo "Created $(TARGET)-sysmodule.zip"

#---------------------------------------------------------------------------------
rebuild: clean all

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).kip $(TARGET).elf $(TARGET).nso $(TARGET).npdm $(TARGET).nsp dist $(TARGET)-sysmodule.zip

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

%.npdm  :   %.npdm.json
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
