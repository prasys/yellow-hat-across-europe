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
UNAME    := $(shell uname -s)
WARN     := -Wall -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-label
# make STUB=1: no game at all (CI). An empty lift stands in for build/gen, so
# the host and pcrecomp's runtime are at least compiled and linked. It is kept
# apart, in build/stub, and never touches a real build.
B        := $(if $(STUB),build/stub,build)
CFLAGS   := $(DEFS) -I$(PCRECOMP)/runtime/recomp32 -I$(HLE) -Isrc -I$(B)/gen $(SDL)
HLE_SRC  := $(filter-out %_selftest.c,$(wildcard $(HLE)/*.c))
HOST_SRC := $(wildcard src/*.c)
ifneq (,$(findstring MINGW,$(UNAME)))
# Windows, in an MSYS2 UCRT64 shell (docs/briefs/05). win32hle is written for
# POSIX: src/win goes first on the include path and supplies what MinGW lacks
# (src/win/compat_win.h says how). Its sockets file is left out; the game has
# no network. SDL is linked without SDL2main: main() is the program's own, and
# it stays a console program so what it prints can be read.
EXE      := .exe
CFLAGS   := -Isrc/win -D_FILE_OFFSET_BITS=64 -DSDL_MAIN_HANDLED $(CFLAGS)
HLE_SRC  := $(filter-out %/wsock32.c,$(HLE_SRC))
HOST_SRC += $(wildcard src/win/*.c)
LIBS     := -lSDL2_ttf -lSDL2 -lwinmm -lpthread -lm
else
EXE      :=
LIBS     := $(shell pkg-config --libs sdl2 SDL2_ttf) -lpthread -lm
ifeq ($(UNAME),Darwin)
LIBS     += -framework AudioToolbox -framework CoreFoundation
else ifneq ($(FLUIDSYNTH),0)
# Linux: the MIDI music on FluidSynth (src/midi.c), when it is installed.
# Without it (or with FLUIDSYNTH=0) the program builds and the music is silent.
ifneq (,$(shell pkg-config --exists fluidsynth && echo yes))
CFLAGS   += -DHAVE_FLUIDSYNTH $(shell pkg-config --cflags fluidsynth)
LIBS     += $(shell pkg-config --libs fluidsynth)
endif
endif
endif
BIN      := $(B)/madeline$(EXE)

HLE_OBJ  := $(patsubst $(HLE)/%.c,$(B)/obj/hle/%.o,$(HLE_SRC))
SRC_OBJ  := $(patsubst src/%.c,$(B)/obj/src/%.o,$(HOST_SRC))
GEN_OBJ  := $(B)/obj/gen/recomp_0000.o $(B)/obj/gen/recomp_dispatch.o

all: $(BIN)

$(BIN): $(HLE_OBJ) $(SRC_OBJ) $(GEN_OBJ)
	$(CC) -o $@ $^ $(LIBS)

$(B)/obj/hle/%.o: $(HLE)/%.c
	@mkdir -p $(@D)
	$(CC) $(OPT) $(WARN) $(CFLAGS) -c $< -o $@
$(B)/obj/src/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(OPT) -Wall $(CFLAGS) -c $< -o $@
$(B)/obj/gen/%.o: $(B)/gen/%.c
	@mkdir -p $(@D)
	$(CC) $(GENOPT) -w $(CFLAGS) -include $(B)/gen/recomp_funcs.h -c $< -o $@

# The lifted C is made from the player's own MADEURO.EXE; it is never committed.
# lift_id.h records which MADEURO.EXE that was (its POSIX cksum and size). It
# is rewritten only when the disc's EXE is a different file, which is what
# makes a different disc lift again; the engine checks it against the disc it
# is started with (src/main.c), since code lifted from one build cannot run
# another's data.
ifdef STUB
build/stub/gen/recomp_0000.c:
	@mkdir -p build/stub/gen
	@printf '#pragma once\n' > build/stub/gen/recomp_funcs.h
	@printf '#define LIFT_CKSUM 0u\n#define LIFT_SIZE 0u\n' > build/stub/gen/lift_id.h
	@printf '#include "recomp_types.h"\nconst recomp_dispatch_entry_t recomp_dispatch_table[] = { { 0, 0 } };\nconst uint32_t recomp_dispatch_count = 0;\n' > build/stub/gen/recomp_dispatch.c
	@printf '/* no game: an empty lift (make STUB=1) */\ntypedef int no_lift;\n' > $@
build/stub/gen/recomp_dispatch.c build/stub/gen/lift_id.h: build/stub/gen/recomp_0000.c
else
build/gen/lift_id.h: $(CD)/MADEURO.EXE FORCE
	@mkdir -p build/gen
	@cksum < "$(CD)/MADEURO.EXE" | awk '{ printf "#define LIFT_CKSUM %su\n#define LIFT_SIZE %su\n", $$1, $$2 }' > $@.new
	@cmp -s $@.new $@ && rm $@.new || mv $@.new $@
build/gen/recomp_0000.c: build/gen/lift_id.h
	scripts/lift.sh "$(CD)"
build/gen/recomp_dispatch.c: build/gen/recomp_0000.c
endif
$(B)/obj/src/main.o: $(B)/gen/lift_id.h
$(SRC_OBJ): src/madeline.h
FORCE:
lift:
	scripts/lift.sh "$(CD)"
$(CD)/MADEURO.EXE:
	@echo "No game found in $(CD). Unpack your own disc first:"; echo "  make extract ISO=/path/to/your.iso"; exit 1
extract:
	scripts/extract_iso.sh "$(ISO)" $(CD)
run: $(BIN)
	$(BIN) "$(CD)"

ifneq (,$(findstring MINGW,$(UNAME)))
# The launcher (Windows): a Win32 window for the settings and a Play button
# (launcher/win), put in a folder with the engine and the DLLs the engine
# needs, so it starts from Explorer. SDL2 there is sdl2-compat, which loads
# SDL3.dll by name at run time: ldd does not show it, so it is named here.
# The folder holds the lift of the player's own game: do not hand it on.
APP      := $(B)/app
APPEXE   := Yellow Hat Across Europe.exe
build/obj/launcher_res.o: launcher/win/launcher.rc launcher/win/launcher.manifest
	@mkdir -p $(@D)
	windres -I launcher/win $< -o $@
build/launcher.exe: launcher/win/launcher.c build/obj/launcher_res.o
	$(CC) -O2 -Wall -municode -mwindows -static -o $@ $^ -lcomctl32 -lcomdlg32 -lshell32 -lole32 -lgdi32 -lmsimg32
# Files are copied over an existing folder rather than the folder being replaced:
# the launcher runs `make engine-into-app` when a disc needs its engine built.
app: $(BIN) build/launcher.exe
	@mkdir -p "$(APP)"
	cp $(BIN) "$(APP)/"
	cp build/launcher.exe "$(APP)/$(APPEXE)"
	@ldd $(BIN) "$$MINGW_PREFIX/bin/SDL3.dll" | awk -v p="$$MINGW_PREFIX/" 'index($$3, p) == 1 { print $$3 }' | sort -u | while read -r f; do cp "$$f" "$(APP)/"; done
	@cp "$$MINGW_PREFIX/bin/SDL3.dll" "$(APP)/"
	@cygpath -m "$$(pwd)" > "$(APP)/source-root.txt"
	@cygpath -m / > "$(APP)/msys-root.txt"
	@echo "built $(APP)/$(APPEXE)"
engine-into-app: $(BIN)
	@mkdir -p "$(APP)"
	cp $(BIN) "$(APP)/"
launch: app
	"$(APP)/$(APPEXE)" &
else
# The launcher (macOS): a SwiftUI window for the settings and a Play button,
# wrapped with the engine as an app. The app holds the lift of the player's
# own game, so it is for the machine it was built on: do not hand it on.
APP      := $(B)/The Little Girl in the Yellow Hat Across Europe.app
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
app: $(BIN) build/launcher build/AppIcon.icns launcher/Info.plist
	@mkdir -p "$(APP)/Contents/MacOS" "$(APP)/Contents/Resources"
	cp launcher/Info.plist "$(APP)/Contents/Info.plist"
	cp build/AppIcon.icns "$(APP)/Contents/Resources/AppIcon.icns"
	cp build/launcher $(BIN) "$(APP)/Contents/MacOS/"
	@pwd > "$(APP)/Contents/Resources/source-root"
	@touch "$(APP)"
	@echo "built $(APP)"
engine-into-app: $(BIN)
	@mkdir -p "$(APP)/Contents/MacOS"
	cp $(BIN) "$(APP)/Contents/MacOS/madeline"
launch: app
	open "$(APP)"
endif
ifeq ($(UNAME),Linux)
# Linux has no launcher app: `make desktop` puts the game in the desktop's
# menu instead, for this user only. It starts this checkout's engine on this
# checkout's disc. The file is named after the program so a Wayland desktop
# matches it to the window (SDL's app id is the program's name).
# Remove it with `make undesktop`.
APPS     := $(or $(XDG_DATA_HOME),$(HOME)/.local/share)
desktop: $(BIN)
	@mkdir -p "$(APPS)/applications" "$(APPS)/icons/hicolor/scalable/apps"
	cp launcher/icon.svg "$(APPS)/icons/hicolor/scalable/apps/madeline.svg"
	@printf '[Desktop Entry]\nType=Application\nName=Yellow Hat Across Europe\nGenericName=Madeline: European Adventures\nComment=Madeline: European Adventures (1996), from your own disc\nExec="%s" "%s"\nIcon=madeline\nTerminal=false\nCategories=Game;KidsGame;\nStartupWMClass=madeline\n' \
	  "$(abspath $(BIN))" "$(abspath $(CD))" > "$(APPS)/applications/madeline.desktop"
	@echo "added $(APPS)/applications/madeline.desktop"
undesktop:
	rm -f "$(APPS)/applications/madeline.desktop" "$(APPS)/icons/hicolor/scalable/apps/madeline.svg"
endif
clean:
	rm -rf build
.PHONY: all lift extract run app engine-into-app launch desktop undesktop clean FORCE
