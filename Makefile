# -------- toolchain --------
CXX      := g++
CC       := gcc
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iinclude -Iinclude/stb
CFLAGS   := -O2 -Wall -Wextra -Iinclude -Iinclude/stb

# -------- platform / linking --------
LDFLAGS :=
LDLIBS  := -lglfw -lGL -ldl -lpthread

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
    LDLIBS := -lglfw -framework Cocoa -framework OpenGL -framework IOKit -framework CoreVideo
endif
ifeq ($(OS),Windows_NT)
    LDLIBS := -lglfw3 -lopengl32 -lgdi32 -luser32
endif

# -------- sources --------
SRC    := src/main.cpp src/glad.c src/collision.cpp src/texture.cpp src/map.cpp src/glb_loader.cpp src/pass.cpp src/framebuffer.cpp src/asset.cpp src/mesh.cpp src/tri_grid.cpp src/probe_grid.cpp
OBJ    := $(SRC:.cpp=.o)
OBJ    := $(OBJ:.c=.o)

TARGET := lumen

# -------- rules --------
.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

debug: CXXFLAGS := -std=c++17 -O0 -g -fsanitize=address -Wall -Wextra -Iinclude -Iinclude/stb
debug: CFLAGS   := -O0 -g -fsanitize=address -Wall -Wextra -Iinclude -Iinclude/stb
debug: LDFLAGS  += -fsanitize=address
debug: clean all

clean:
	rm -f $(OBJ) $(TARGET)
