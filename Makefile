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
CFLAGS   := $(DEFS) -I$(PCRECOMP)/runtime/recomp32 -I$(HLE) -Isrc -Ibuild/gen $(SDL)
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
endif
endif
BIN      := build/madeline$(EXE)

HLE_OBJ  := $(patsubst $(HLE)/%.c,build/obj/hle/%.o,$(HLE_SRC))
SRC_OBJ  := $(patsubst src/%.c,build/obj/src/%.o,$(HOST_SRC))
GEN_OBJ  := build/obj/gen/recomp_0000.o build/obj/gen/recomp_dispatch.o

all: $(BIN)

$(BIN): $(HLE_OBJ) $(SRC_OBJ) $(GEN_OBJ)
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
run: $(BIN)
	$(BIN) "$(CD)"

ifneq (,$(findstring MINGW,$(UNAME)))
# The launcher (Windows): a Win32 window for the settings and a Play button
# (launcher/win), put in a folder with the engine and the DLLs the engine
# needs, so it starts from Explorer. SDL2 there is sdl2-compat, which loads
# SDL3.dll by name at run time: ldd does not show it, so it is named here.
# The folder holds the lift of the player's own game: do not hand it on.
APP      := build/app
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
endif
clean:
	rm -rf build
.PHONY: all lift extract run app engine-into-app launch clean FORCE
