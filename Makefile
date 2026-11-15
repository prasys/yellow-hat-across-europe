# Madeline: European Adventures on a modern Mac (or Linux), by static recompilation.
#   make extract ISO=/path/to/your.iso    unpack your own disc into game/cd
#   make                                  lift MADEURO.EXE and build build/madeline
#   make run                              play
#   make launch                           build the app (settings + Play button) and open it
PCRECOMP := third_party/pcrecomp
HLE      := $(PCRECOMP)/runtime/win32hle
CD       ?= game/cd
CC       ?= cc
OPT      ?= -O2
GENOPT   ?= -O1
# The guest's 4 GB sits at one fixed host address (pcrecomp's "based" memory).
DEFS     := -DRECOMP_MEM_BASE=0x100000000000ull
SDL      := $(shell pkg-config --cflags sdl2 SDL2_ttf)
LIBS     := $(shell pkg-config --libs sdl2 SDL2_ttf) -lpthread -lm
ifeq ($(shell uname -s),Darwin)
LIBS     += -framework AudioToolbox -framework CoreFoundation
endif
WARN     := -Wall -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-label
CFLAGS   := $(DEFS) -I$(PCRECOMP)/runtime/recomp32 -I$(HLE) -Isrc -Ibuild/gen $(SDL)

HLE_SRC  := $(filter-out %_selftest.c,$(wildcard $(HLE)/*.c))
HLE_OBJ  := $(patsubst $(HLE)/%.c,build/obj/hle/%.o,$(HLE_SRC))
SRC_OBJ  := $(patsubst src/%.c,build/obj/src/%.o,$(wildcard src/*.c))
GEN_OBJ  := build/obj/gen/recomp_0000.o build/obj/gen/recomp_dispatch.o

all: build/madeline

build/madeline: $(HLE_OBJ) $(SRC_OBJ) $(GEN_OBJ)
	$(CC) -o $@ $^ $(LIBS)

build/obj/hle/%.o: $(HLE)/%.c
	@mkdir -p $(@D)
	$(CC) $(OPT) $(WARN) $(CFLAGS) -c $< -o $@
build/obj/src/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(OPT) -Wall $(CFLAGS) -c $< -o $@
build/obj/gen/%.o: build/gen/%.c
	@mkdir -p $(@D)
	$(CC) $(GENOPT) -w $(CFLAGS) -include build/gen/recomp_funcs.h -c $< -o $@

# The lifted C is made from the player's own MADEURO.EXE; it is never committed.
# lift_id.h records which MADEURO.EXE that was (its POSIX cksum and size). It
# is rewritten only when the disc's EXE is a different file, which is what
# makes a different disc lift again; the engine checks it against the disc it
# is started with (src/main.c), since code lifted from one build cannot run
# another's data.
build/gen/lift_id.h: $(CD)/MADEURO.EXE FORCE
	@mkdir -p build/gen
	@cksum < "$(CD)/MADEURO.EXE" | awk '{ printf "#define LIFT_CKSUM %su\n#define LIFT_SIZE %su\n", $$1, $$2 }' > $@.new
	@cmp -s $@.new $@ && rm $@.new || mv $@.new $@
build/gen/recomp_0000.c: build/gen/lift_id.h
	scripts/lift.sh "$(CD)"
build/gen/recomp_dispatch.c: build/gen/recomp_0000.c
build/obj/src/main.o: build/gen/lift_id.h
$(SRC_OBJ): src/madeline.h
FORCE:
lift:
	scripts/lift.sh "$(CD)"
$(CD)/MADEURO.EXE:
	@echo "No game found in $(CD). Unpack your own disc first:"; echo "  make extract ISO=/path/to/your.iso"; exit 1
extract:
	scripts/extract_iso.sh "$(ISO)" $(CD)
run: build/madeline
	build/madeline "$(CD)"

# The launcher (macOS): a SwiftUI window for the settings and a Play button,
# wrapped with the engine as an app. The app holds the lift of the player's
# own game, so it is for the machine it was built on: do not hand it on.
APP      := build/The Little Girl in the Yellow Hat Across Europe.app
SWIFTC   ?= swiftc
build/launcher: $(filter-out launcher/make_icon.swift,$(wildcard launcher/*.swift))
	@mkdir -p build
	$(SWIFTC) -O -parse-as-library -target $(shell uname -m)-apple-macos13.0 $^ -o $@
# The icon is drawn by a script (launcher/make_icon.swift), then cut to the sizes macOS wants.
build/AppIcon.icns: launcher/make_icon.swift
	@mkdir -p build/AppIcon.iconset
	swift launcher/make_icon.swift build/icon-1024.png
	@for s in 16 32 128 256 512; do \
	  sips -z $$s $$s build/icon-1024.png --out build/AppIcon.iconset/icon_$${s}x$${s}.png >/dev/null; \
	  d=$$((s * 2)); sips -z $$d $$d build/icon-1024.png --out build/AppIcon.iconset/icon_$${s}x$${s}@2x.png >/dev/null; \
	done
	iconutil -c icns build/AppIcon.iconset -o $@
# Files are copied over an existing app rather than the app being replaced:
# the launcher runs `make engine-into-app` on itself when a disc needs its engine built.
app: build/madeline build/launcher build/AppIcon.icns launcher/Info.plist
	@mkdir -p "$(APP)/Contents/MacOS" "$(APP)/Contents/Resources"
	cp launcher/Info.plist "$(APP)/Contents/Info.plist"
	cp build/AppIcon.icns "$(APP)/Contents/Resources/AppIcon.icns"
	cp build/launcher build/madeline "$(APP)/Contents/MacOS/"
	@pwd > "$(APP)/Contents/Resources/source-root"
	@touch "$(APP)"
	@echo "built $(APP)"
engine-into-app: build/madeline
	@mkdir -p "$(APP)/Contents/MacOS"
	cp build/madeline "$(APP)/Contents/MacOS/madeline"
launch: app
	open "$(APP)"
clean:
	rm -rf build
.PHONY: all lift extract run app engine-into-app launch clean FORCE
