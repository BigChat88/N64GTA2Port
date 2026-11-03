# GTA 2 for N64: gta2_re engine (GTA 2 PC decomp) compiled for N64 + platform layer (src/n64).
# Run bash setup_engine.sh once before compiling. To pack a ROM without compiling, see tools/pack_rom.py.
BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/o
ROM_NAME  = gta2_recomp
ROM_OUTPUT = GTA 2 N64.z64
N64_ROM_TITLE = "GTA 2 N64"
N64_ROM_SAVETYPE = flashram
N64_ROM_EXPANSIONPAK = required
N64_INST ?= /opt/libdragon
# Preview APIs in use: fmv_play, display_change, vi_set_borders, flashram_*.
LIBDRAGON_PREVIEW = 2
include $(N64_INST)/include/n64.mk
N64_CXXFLAGS ?= $(N64_CFLAGS)

# The ROM is built next to the Makefile (n64.mk's %.z64 rule) and copied to output/,
# where build.cmd / tools/pack_rom.py also write it.
all: $(ROM_NAME).z64
	@mkdir -p output
	@echo "    [COPY] output/$(ROM_OUTPUT)"
	@cp $< "output/$(ROM_OUTPUT)"
.PHONY: all

# --- game data in the ROM (DragonFS): your own copy, personal use (see gamedata/README.md) ---
GAME_DATA ?= gamedata
FS_DIR     = $(BUILD_DIR)/filesystem
PYTHON    ?= python3
# Options of tools/stage_data.py, which holds the defaults. For a smaller ROM, e.g.: MAPS="bil".
# INCLUDE_FRONTEND=0 removes the 25 menu screens (~9.6 MB; the menu does not open: debug ROMs only, where Z at power-on goes straight into a map).
# INCLUDE_INTRO=0 leaves out the intro movie (about 9.5 MB; converting it, and limiting the radios, needs ffmpeg).
INCLUDE_FRONTEND ?= 1
INCLUDE_INTRO ?= 1
RADIO_RATE ?= 12000
RADIO_QUALITY ?= 35
STAGE_FLAGS = $(if $(MAPS),--maps "$(MAPS)") $(if $(BONUS),--bonus "$(BONUS)") $(if $(RADIOS),--radios "$(RADIOS)") \
              $(if $(SFX_MAPS),--sfx-maps "$(SFX_MAPS)") $(if $(filter 1,$(INCLUDE_FRONTEND)),,--no-frontend) $(if $(filter 1,$(INCLUDE_INTRO)),,--no-intro) \
              --radio-rate $(RADIO_RATE) --radio-quality $(RADIO_QUALITY)

ENGINE := $(shell tr -d '\r' < src/game_srcs.txt)
ENGINE += registry crt_stubs port_static_init
PLAT   := win32_n64 dinput_n64 crt_n64 settings_n64 miles_n64 bink_n64 gbh_n64 vid_n64 frame_n64 exports_n64 net_n64 n64_extra game_entry ramfs_n64

ENGINE_OBJS := $(addprefix $(OBJ_DIR)/src/gta2_re/Source/,$(addsuffix .o,$(ENGINE)))
PLAT_OBJS   := $(addprefix $(OBJ_DIR)/src/n64/,$(addsuffix .o,$(PLAT)))
OBJS := $(ENGINE_OBJS) $(PLAT_OBJS) $(OBJ_DIR)/src/n64/main_n64.o $(OBJ_DIR)/src/n64/exc_fix.o $(OBJ_DIR)/src/n64/render_n64.o $(OBJ_DIR)/src/n64/audio_n64.o $(OBJ_DIR)/src/n64/intro_n64.o $(OBJ_DIR)/src/n64/logo_n64.o $(OBJ_DIR)/src/n64/savefs_n64.o

CXX_STD ?= -std=gnu++17
CXX_ENGINE = $(CXX_STD) -fshort-wchar -fms-extensions -fpermissive -w -DGTA2_PORT=1 -DN64 -fno-exceptions -fno-rtti \
             -include src/compat/gcc_compat.h -include src/compat/port_prefix.h \
             -Isrc/compat -Isrc/compat_ci -Isrc/n64 -Isrc/gta2_re/Source -Isrc/gta2_re -MMD -MP $(EXTRA_CFLAGS)

# Engine: optimize for size (N64 RAM is the bottleneck; the code takes ~1.9 MB of the 8 MB).
# Heaviest files per frame (map, sprites, collision, car physics, math): -O2 (faster, larger code).
HOT_ENGINE := MapRenderer sprite SpriteRenderer_1C SpriteGrid_400 Draw map_0x370 collide CarPhysics_B0 Fix16_Point fix16 ang16 angle
HOT_OBJS := $(addprefix $(OBJ_DIR)/src/gta2_re/Source/,$(addsuffix .o,$(HOT_ENGINE)))
$(filter-out $(HOT_OBJS),$(ENGINE_OBJS)): N64_CXXFLAGS += -Os
$(HOT_OBJS): N64_CXXFLAGS += -O2

# crt_stubs.cpp uses dynamic exception specifications (invalid in C++17); the n64 layer was validated in C++14.
$(OBJ_DIR)/src/gta2_re/Source/crt_stubs.o $(PLAT_OBJS): CXX_STD = -std=gnu++14

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "    [CXX] $<"
	$(N64_CXX) $(N64_CXXFLAGS) $(CXX_ENGINE) -c $< -o $@

# Video safe margin (VI dots, 640x480 base). 0 = full image (emulator); increase if the TV/upscaler crops the edges.
OVERSCAN_X ?= 0
OVERSCAN_Y ?= 0
$(OBJ_DIR)/src/n64/main_n64.o: N64_CFLAGS += -DN64_OVERSCAN_X=$(OVERSCAN_X) -DN64_OVERSCAN_Y=$(OVERSCAN_Y) $(EXTRA_CFLAGS)
$(OBJ_DIR)/src/n64/render_n64.o: N64_CFLAGS += $(EXTRA_CFLAGS)
$(OBJ_DIR)/src/n64/audio_n64.o: N64_CFLAGS += $(EXTRA_CFLAGS)

# Platform layer optimization (renderer/conversion/drawing): small code, hot every frame.
PORT_OPT ?= -O3
$(PLAT_OBJS): N64_CXXFLAGS += $(PORT_OPT)
$(OBJ_DIR)/src/n64/render_n64.o: N64_CFLAGS += $(PORT_OPT)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "    [CC]  $<"
	$(N64_CC) $(N64_CFLAGS) -std=gnu11 -MMD -MP -c $< -o $@

# Dragon logo at power-on: assets/intro (see its README) is converted to rom:/intro/*.sprite.
LOGO_SPRITES = $(patsubst assets/intro/%.png,$(BUILD_DIR)/intro/%.sprite,$(wildcard assets/intro/*.png))
$(BUILD_DIR)/intro/%.sprite: assets/intro/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	$(N64_MKSPRITE) -f I8 -o $(dir $@) "$<"

stage-data:
	@$(PYTHON) tools/stage_data.py --gamedata "$(GAME_DATA)" --out "$(FS_DIR)" --audioconv "$(N64_INST)/bin/audioconv64" $(STAGE_FLAGS)

# Explicit rule (beats the generic n64.mk rule): DragonFS root = $(FS_DIR), hence rom:/data/x.
$(BUILD_DIR)/$(ROM_NAME).dfs: stage-data $(LOGO_SPRITES)
	@mkdir -p $(dir $@) $(FS_DIR)/intro
	@cp $(LOGO_SPRITES) $(FS_DIR)/intro/
	@echo "    [DFS] $@"
	$(N64_MKDFS) $@ $(FS_DIR)

$(BUILD_DIR)/$(ROM_NAME).elf: N64_LDFLAGS += $(EXTRA_LDFLAGS)
$(BUILD_DIR)/$(ROM_NAME).elf: $(OBJS)

-include $(OBJS:.o=.d)

$(ROM_NAME).z64: $(BUILD_DIR)/$(ROM_NAME).dfs

# ---------------------------------------------------------------------------
# Prebuilt engine (no game data needed)
# ---------------------------------------------------------------------------
# The compiled engine is the same for everyone: only the ROM filesystem holds
# the user's GTA 2 files. `make engine` builds just the code, the same way
# n64.mk's %.z64 rule does, so CI can publish it in a Release (see
# .github/workflows/release.yml) and tools/pack_rom.py can pack a ROM without
# the N64 toolchain.
ENGINE_DIR = $(BUILD_DIR)/engine
ENGINE_ELF = $(BUILD_DIR)/$(ROM_NAME).elf

# n64.mk only switches to the N64 toolchain for %.z64 targets.
engine: CC=$(N64_CC)
engine: CXX=$(N64_CXX)
engine: AS=$(N64_AS)
engine: LD=$(N64_LD)
engine: CFLAGS+=$(N64_CFLAGS)
engine: CXXFLAGS+=$(N64_CXXFLAGS)
engine: ASFLAGS+=$(N64_ASFLAGS)
engine: RSPASFLAGS+=$(N64_RSPASFLAGS)
engine: LDFLAGS+=$(N64_LDFLAGS)
engine: $(ENGINE_ELF) $(LOGO_SPRITES)
	@echo "    [ENGINE] $(ENGINE_DIR)"
	@rm -rf $(ENGINE_DIR)
	@mkdir -p $(ENGINE_DIR)/intro
	$(N64_SYM) --all $< $<.sym
	cp $< $<.stripped
	$(N64_STRIP) -s $<.stripped
	$(N64_ELFCOMPRESS) -o $(dir $<) -c $(N64_ROM_ELFCOMPRESS) $<.stripped
	cp $<.stripped $(ENGINE_DIR)/$(ROM_NAME).elf.stripped
	cp $<.sym $(ENGINE_DIR)/$(ROM_NAME).elf.sym
	cp $(LOGO_SPRITES) $(ENGINE_DIR)/intro/
	$(if $(N64_TOOLFILES),cp $(N64_TOOLFILES) $(ENGINE_DIR)/)
	@# ROM header settings, so pack_rom.py stays in sync with this Makefile.
	@printf 'title=%s\ncategory=%s\nsavetype=%s\nexpansionpak=%s\nregionfree=%s\n' \
		$(N64_ROM_TITLE) $(strip $(N64_ROM_CATEGORY)) $(strip $(N64_ROM_SAVETYPE)) $(strip $(N64_ROM_EXPANSIONPAK)) \
		$(if $(strip $(N64_ROM_REGIONFREE)),1,0) > $(ENGINE_DIR)/rom.cfg
.PHONY: engine

clean:
	rm -rf $(BUILD_DIR) $(ROM_NAME).z64 "output/$(ROM_OUTPUT)"

.PHONY: clean stage-data
