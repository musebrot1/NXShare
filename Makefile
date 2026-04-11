#---------------------------------------------------------------------------------
# NXShare - Nintendo Switch Homebrew
# Makefile for devkitPro / libnx
#---------------------------------------------------------------------------------

APP_TITLE   := NXShare
APP_AUTHOR  := musebrot
APP_VERSION := 1.3.0

TARGET      := NXShare
BUILD       := build
SOURCES     := source
INCLUDES    := include

#---------------------------------------------------------------------------------
# devkitPro toolchain
#---------------------------------------------------------------------------------
ifneq ($(DEVKITPRO),)
    include $(DEVKITPRO)/libnx/switch_rules
else
    $(error "DEVKITPRO environment variable not set. Did you install devkitPro?")
endif

#---------------------------------------------------------------------------------
# Compiler flags
#---------------------------------------------------------------------------------
ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
ARCHASM     := -march=armv8-a+crc+crypto
CFLAGS      := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CXXFLAGS    := $(CFLAGS) -std=c++17
ASFLAGS     := -g $(ARCHASM)
LDFLAGS     := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS        := -lnx

#---------------------------------------------------------------------------------
# Source files (html_data.cpp is pre-generated from data/web/index.html)
#---------------------------------------------------------------------------------
CFILES      := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.c))
CXXFILES    := $(foreach dir,$(SOURCES),$(wildcard $(dir)/*.cpp))
OFILES      := $(addprefix $(BUILD)/,$(notdir $(CFILES:.c=.o) $(CXXFILES:.cpp=.o)))

#---------------------------------------------------------------------------------
# Include paths
#---------------------------------------------------------------------------------
INCLUDE     := $(foreach dir,$(INCLUDES),-I$(dir)) \
               -I$(DEVKITPRO)/libnx/include \
               -I$(PORTLIBS)/include

LIBDIRS     := $(DEVKITPRO)/libnx $(PORTLIBS)

#---------------------------------------------------------------------------------
# Build rules
#---------------------------------------------------------------------------------
.PHONY: all clean

all: $(BUILD) $(TARGET).nro

$(BUILD):
	@mkdir -p $@

$(TARGET).nro: $(TARGET).elf $(BUILD)/$(TARGET).nacp
	@elf2nro $< $@ --icon=icon.jpg --titleid=0x0100000000000000 \
	    --nacp=$(BUILD)/$(TARGET).nacp
	@echo "Built: $(TARGET).nro"

$(TARGET).elf: $(OFILES)
	@echo "Linking..."
	@$(CXX) $(LDFLAGS) -o $@ $^ $(foreach dir,$(LIBDIRS),-L$(dir)/lib) $(LIBS)

$(BUILD)/$(TARGET).nacp:
	@nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

# Compile C++ sources
$(BUILD)/%.o: source/%.cpp | $(BUILD)
	@echo "Compiling $<"
	@$(CXX) $(CXXFLAGS) $(INCLUDE) -c $< -o $@

# Compile C sources
$(BUILD)/%.o: source/%.c | $(BUILD)
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

clean:
	@rm -rf $(BUILD) $(TARGET).nro $(TARGET).elf
	@echo "Cleaned."
