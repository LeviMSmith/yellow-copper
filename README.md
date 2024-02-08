# Voyages & Verve

Fully simulated pixel world

## Building and running

Should build and run anywhere SDL2 will compile for, no extra configuration
required.

### Build dependancies

All code dependancies should be in vendor and built automatically by cmake,
so you'll just need that and a compiler.

- [cmake](https://cmake.org/download/)

### Building

Configure cmake:

`cmake =S . -B ./build -DCMAKE_BUILD_TYPE=Debug`

Build the generated cmake config

`cmake --build ./build`

Now the executable is built in, likely, build/voyages-and-verve
