CXX ?= g++
PKG_CONFIG ?= pkg-config
BUILD_DIR ?= build

PACKAGES := hyprland pixman-1 libdrm glesv2
HYPRLAND_VERSION := $(shell $(PKG_CONFIG) --modversion hyprland 2>/dev/null)
HYPRLAND_SERIES := $(word 2,$(subst ., ,$(HYPRLAND_VERSION)))
PLUGIN_VERSION := $(shell sed -nE 's/^[[:space:]]*"version":[[:space:]]*"([0-9]+\.[0-9]+)",?[[:space:]]*$$/\1/p' manifest.json)
TARGET := $(BUILD_DIR)/waterclick.so
OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/EffectConfig.o $(BUILD_DIR)/WaterPass.o

CPPFLAGS += $(shell $(PKG_CONFIG) --cflags $(PACKAGES)) -I$(BUILD_DIR) -DWATERCLICK_HYPRLAND_SERIES=$(HYPRLAND_SERIES)
CPPFLAGS += -DWATERCLICK_VERSION='"$(PLUGIN_VERSION)"'
CXXFLAGS += -std=c++23 -O2 -fPIC -Wall -MMD -MP
LDLIBS += $(shell $(PKG_CONFIG) --libs $(PACKAGES))

ifeq ($(findstring clang,$(CXX)),)
CXXFLAGS += -fno-gnu-unique
endif

.PHONY: all clean
all: $(TARGET)

ifneq ($(MAKECMDGOALS),clean)
ifeq ($(filter 0.56.% 0.57.%,$(HYPRLAND_VERSION)),)
$(error Hyprland 0.56 or 0.57 development headers are required; found '$(HYPRLAND_VERSION)')
endif
ifeq ($(PLUGIN_VERSION),)
$(error The Water Click version in manifest.json must be MAJOR.MINOR)
endif
endif

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.frag.inc: native/shaders/%.frag | $(BUILD_DIR)
	{ printf 'R"GLSL('; cat $<; printf ')GLSL"\n'; } > $@

$(BUILD_DIR)/main.o: manifest.json
$(BUILD_DIR)/WaterPass.o: $(BUILD_DIR)/water.frag.inc $(BUILD_DIR)/effects.frag.inc

$(BUILD_DIR)/%.o: native/src/%.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CXX) -shared $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -rf -- "$(BUILD_DIR)"

-include $(OBJECTS:.o=.d)
