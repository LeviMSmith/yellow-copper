# Voyages & Verve

Fully simulated pixel world

## Building and running

Should build and run anywhere SDL2 will compile for. Requires the freetype library.
Honestly not sure if that's a compile time dependancy or runtime.

### Build dependancies

Most code dependancies should be in vendor and built automatically by cmake,
so you'll just need that and a compiler. SDL_ttf might require the
freetype library installed on your system.

Make sure to also clone the submodules to actually get that code into vendor:

`git submodule update --init --recursive --depth 1`

- [cmake](https://cmake.org/download/)
- [freetype](https://freetype.org/)

### Building

Configure cmake:

`cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Debug`

Build the generated cmake config

`cmake --build ./build`

Now the executable is built in, likely, build/{platform}/{release}/voyages-and-verve
