# Madeline: European Adventures on a modern Mac (or Linux), by static recompilation.
#   make extract ISO=/path/to/your.iso    unpack your own disc into game/cd
#   make                                  lift MADEURO.EXE and build build/madeline
#   make run                              play
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
build/gen/recomp_0000.c: $(CD)/MADEURO.EXE
	scripts/lift.sh $(CD)
build/gen/recomp_dispatch.c: build/gen/recomp_0000.c
lift:
	scripts/lift.sh $(CD)
$(CD)/MADEURO.EXE:
	@echo "No game found in $(CD). Unpack your own disc first:"; echo "  make extract ISO=/path/to/your.iso"; exit 1
extract:
	scripts/extract_iso.sh "$(ISO)" $(CD)
run: build/madeline
	build/madeline $(CD)
clean:
	rm -rf build
.PHONY: all lift extract run clean
