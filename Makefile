#---------------------------------------------------------------------------------------
# Makefile for pctltcp-web (Nintendo Switch NRO)
#---------------------------------------------------------------------------------------

.SUFFIXES:

#---------------------------------------------------------------------------------------
# Build config
#---------------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

include $(DEVKITPRO)/libnx/switch_rules

TARGET		:= pctltcp-web
BUILD		:= build
SOURCES		:= source
DATA		:= data
INCLUDES	:= source

#---------------------------------------------------------------------------------------
# Options
#---------------------------------------------------------------------------------------
ARCH	:= -march=armv8-a -mtune=cortex-a57 -mtp=soft
CFLAGS	:= -g -O2 -Wall -Wextra -ffunction-sections -fdata-sections \
           $(ARCH) $(INCLUDES) -D__SWITCH__ $(DEFINES)
CFLAGS	+= $(INCLUDE) -I$(LIBNX)/include

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS	:= -g $(ARCH)
LDFLAGS	 = -specs=switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lnx

#---------------------------------------------------------------------------------------
# Source files
#---------------------------------------------------------------------------------------
CFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

BINFILES	:= $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------------
# Output
#---------------------------------------------------------------------------------------
OFILES_BIN	:= $(addsuffix .bin.o,$(BINFILES))
OFILES_SRC	:= $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
OFILES		:= $(OFILES_BIN) $(OFILES_SRC)

export HFILES_BIN	:= $(addsuffix .h,$(subst .,_,$(BINFILES)))

export OFILES		:= $(OFILES)
export OFILES_SRC	:= $(OFILES_SRC)
export OUTPUT		:= $(CURDIR)/$(TARGET)
export TOPDIR		:= $(CURDIR)

export VPATH		:= $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                     $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR		:= $(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------------
# Rules
#---------------------------------------------------------------------------------------
.PHONY: all clean

all: $(BUILD) $(OUTPUT).nro

$(BUILD):
	@[ -d $@ ] || mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).elf $(TARGET).nacp $(TARGET).icon

#---------------------------------------------------------------------------------------
# Switch NRO
#---------------------------------------------------------------------------------------
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp $(OUTPUT).icon

$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC): $(HFILES_BIN)

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
