# Apple2e

Yet another Apple //e emulator.

I wrote this not because the world needed another //e emulator, but because I wanted to have the fun of building a thing from scratch to run my old Apple //e software.

At the time of writing, the emulator handles only 40-column text mode and no floppy.

Thanks to Lawrence Kesteloot (@lkesteloot) for keyboard code, and Bart Grantham (@bartgrantham) for extracting all our old floppy disk images!

Thanks to Mike Chambers (miker00lz@gmail.com) for his 6502 CPU emulator, which I used as a reference when mine hung on "PRINT 5".

Usage:

    apple2e [-debugger] <romfile>

Useful debugger commands:

    reset # Press CTRL-RESET
    reboot # Press CTRL-OpenApple-RESET
    debug N # Set debug flags to N (decimal). See apple2e.cpp for flags
    go # Exit debugging, free-run.  Press CTRL-B to break back into the debugger
    # Enter a blank line to step one instruction

