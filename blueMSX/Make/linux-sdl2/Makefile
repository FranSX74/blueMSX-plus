# blueMSX+ Linux proof of concept: core + legacy SDL frontend ported to SDL2.
# Usage (from this directory):  make -j$(nproc)
ROOT     := ../..
OUT      := objs
TARGET   := bluemsx-plus-sdl2

SRCS     := $(shell cat sources.txt)
OBJS     := $(addprefix $(OUT)/,$(patsubst %.cpp,%.o,$(SRCS:.c=.o)))
INCDIRS  := $(shell cd $(ROOT) && find Src -type d -not -path 'Src/Win32*' -not -path 'Src/ThirdParty*' -not -path 'Src/Linux*')
INCLUDE  := $(addprefix -I$(ROOT)/,$(INCDIRS))

SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2)

DEFS     := -DLSB_FIRST -DNO_ASM -DUSE_SDL -DNO_FILE_HISTORY -include $(ROOT)/Src/Sdl/SdlCompat.h
# -fpermissive: GCC 14+ turns the legacy C function-pointer casts into errors
CFLAGS   := -std=gnu11  -O2 -g -fpermissive -w $(DEFS) $(SDL_CFLAGS) $(INCLUDE)
CXXFLAGS := -std=gnu++20 -O2 -g -fpermissive -w $(DEFS) $(SDL_CFLAGS) $(INCLUDE)
LIBS     := $(SDL_LIBS) -lz -lpthread -lm

# e.g. make EXTRA=-fsanitize=address OUT=objs-asan TARGET=bluemsx-asan
CFLAGS   += $(EXTRA)
CXXFLAGS += $(EXTRA)
LIBS     += $(EXTRA)

all: $(TARGET)

$(TARGET): $(OBJS)
	g++ -o $@ $^ $(LIBS)

$(OUT)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) -c $< -o $@

$(OUT)/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	g++ $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT) $(TARGET)

.PHONY: all clean
