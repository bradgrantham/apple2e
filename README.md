# Apple2e

Yet another Apple //e emulator.

I wrote this not because the world needed another //e emulator, but because I wanted to have the fun of building a thing from scratch to run my old Apple //e software.

At the time of writing, the emulator handles only 40-column text mode and no floppy.

There are several AppleSoft files in this project (`*.A`) that can be copied into the clipboard and then pasted into the emulator window with CMD-V.

Thanks to [Lawrence Kesteloot](http://github.com/lkesteloot) for keyboard code, and [Bart Grantham](http://github.com/bartgrantham) for extracting all our old floppy disk images!

Thanks to Mike Chambers (miker00lz@gmail.com) for his 6502 CPU emulator, which I used as a reference when mine hung on "PRINT 5".

Requirements for building:

* GLFW
* OpenGL 3.2-compatible system
* C++11
* Currently the project only builds on MacOSX because of the linker line in `Makefile`, but the C++ code itself should be cross-platform.

On MacOSX with MacPorts, the GLFW dependency can be satisfied with `sudo port install glfw`.  According to https://support.apple.com/en-us/HT202823, almost all modern Macs should have OpenGL 3.2 or later.  On my machine, I've been compiling with a g++ that outputs `Apple LLVM version 8.0.0 (clang-800.0.42.1)` for `g++ -v`.

Usage:

    apple2e [options] <romfile>

Options:

    -debugger # start in the debugger
    -fast     # start with CPU running as fast as it can run

Useful debugger commands:

    reset # Press CTRL-RESET
    reboot # Press CTRL-OpenApple-RESET
    fast # run CPU as fast as it can go
    slow # Approximate CPU at 1.023 MHz
    debug N # Set debug flags to N (decimal). See apple2e.cpp for flags
    go # Exit debugging, free-run.  Press CTRL-B to break back into the debugger
    # Enter a blank line to step one instruction

