system = $(shell uname -s)

.PHONY: all configure build run

# Default target executed when no arguments are given to make.
all: configure build run

# Target for configuring the project with CMake.
configure:
	cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Debug

# Target for building the project using the previously generated CMake configuration.
build:
	cmake --build ./build

# Target for running the executable.
run: build
	./build/$(system)/Debug/./voyages-and-verve

# Optionally, you can include a clean target to remove build artifacts.
clean:
	rm -rf ./build
