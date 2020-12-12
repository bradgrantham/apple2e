# Apple2e

Yet another Apple //e emulator.

![Lode Runner](http://plunk.org/~grantham/apple2e_loderunner.gif "Lode Runner")

I wrote this not because the world needed another //e emulator, but because I wanted to have the fun of building a thing from scratch to run my old Apple //e software.

I wrote a little about my motivation and the process of writing this emulator [on my personal web page](http://plunk.org/~grantham/apple2e.html).

There are several AppleSoft files in this project (`*.A`) that can be copied into the clipboard and then pasted into the emulator window with CMD-V.

Thanks to [Lawrence Kesteloot](http://github.com/lkesteloot) for the original keyboard code, and [Bart Grantham](http://github.com/bartgrantham) for extracting all our old floppy disk images!

Thanks to Mike Chambers (miker00lz@gmail.com) for his 6502 CPU emulator, which I used as a reference when mine hung on "PRINT 5".

Thanks to JLH, from whom I have copied the floppy nybblization code.  His Apple 2 emulator, released under the GPL, is at [http://shamusworld.gotdns.org/apple2/](http://shamusworld.gotdns.org/apple2/).

Requirements for building:

* GLFW
* libao
* OpenGL 3.2
* C++11
* Builds on MacOS using "Makefile" and Linux (tested on Ubuntu only) using "Makefile.linux"

On MacOSX with MacPorts, the GLFW and libao dependency can be satisfied with `glfw` and `libao` ports.  According to https://support.apple.com/en-us/HT202823, all modern Macs have OpenGL 3.2 or later.  On my machine, I've been compiling with a g++ that outputs `Apple LLVM version 8.0.0 (clang-800.0.42.1)` for `g++ -v`.

Usage:

    apple2e [options] ROM.8000.to.FFFF.bin

Options:

    -debugger # start in the debugger
    -fast     # start with CPU running as fast as it can run
    -backspace-is-delete # Backspace key (Delete on Macs) should send DELETE
    -diskII diskIIrom.bin {floppy1image.dsk|none} {floppy2image.dsk|none}

Examples of operation:

    # Use original Apple ][ Integer BASIC ROM, no floppy controller,
    # at maximum available clock rate.
    apple2e -fast apple2_intbasic.rom

    # Use updated Apple ][ ROM, no floppy controller, and attempt to
    # run at 1.023 MHz.
    apple2e -fast apple2o.rom

    # Use Apple //e ROM, add diskII controller with two floppies,
    # put LodeRunner.dsk in drive 1 and nothing in drive 2. Attempt
    # to run at 1.023 MHz.
    apple2e -diskII diskII.c600.c67f.bin LodeRunner.dsk none apple2e_a.rom

Useful debugger commands:

    reset # Press CTRL-RESET
    reboot # Press CTRL-OpenApple-RESET
    fast # run CPU as fast as it can go
    slow # Approximate CPU at 1.023 MHz
    debug N # Set debug flags to N (decimal). See apple2e.cpp for flags
    go # Exit debugging, free-run.
    # Enter a blank line to step one instruction

When the window opens, the emulator displays a user interface panel to the right of the graphics screen.  The buttons and icons are as follows:
* RESET - simulate pressing CONTROL and RESET keys and releasing
* REBOOT - simulate pressing CONTROL and Open-Apple and RESET keys and releasing
* FAST - toggle between running at 1.023MHz and running the CPU as fast as possible (audio will stop in "fast" mode)
* CAPS - toggle caps lock forcibly on or off.
* COLOR - switch between color hi-res graphics and monochrome.
* PAUSE - pause or resume running the CPU.
* Floppy drive icons: Drag and drop floppy `.dsk` files onto a drive to "insert" the flopy disk.  Click the drive icon to "eject" the floppy disk.
* Drag a text file onto the text area to past the file as keyboard input.

If no joystick or gamepad is configured, the Apple 2 screen acts as a joystick.  To configure a joystick, store the GLFW numbers of the two axes and two buttons in "joystick.ini".  A very skilled practitioner may be able to print the joysticks, axes, and buttons by modifying interface.cpp.

