# Apple //e Emulator

## History

We had an Apple //e in the 80's. It was really the first computer I started programming in earnest.  I remember loving it so much, but then we got an Amiga 500.  I spent a lot of time upgrading myself from 6502 assembly and BASIC and a monochrome display to C and actual color.  The //e moved to the basement and gathered dust except the occasional nostalgic boot.

Run the clock forward twenty-five years…  In 2010 my brother, Bart, extracted verbatim byte images from all of the old Apple ][ floppies that weren’t rotted.  When he dropped the archive in my hands, I spent a little time trying in all those images in the great Apple ][ emulator, “Virtual ][“, but then shelved it all.  I didn’t have much interest in revisiting old computers at the time.  I had more important things to do, you know?

## Implementation

Six more years pass.  In November 2016, a little burnt out during a pass of restructuring and cleaning-up my old code, I was looking for something fun to write from scratch.  In the previous year, with Lawrence Kesteloot I had built a Z-80 computer then built an ARM tablet and ported some ancient K&R C graphics demos to it, and I guess I really caught the retrocomputing bug.  I decided to start with nothing but a compiler and the ROM file from our Apple //e.  The result, including graphics, floppy, and audio support, is [on Github](https://github.com/bradgrantham/apple2e).

I figured I’d start with `main()` containing just `fopen()` and `fread()` to get the ROM into an array of `unsigned char` and a `switch` statement for decoding instructions stuffed into a `while(true)`.  I think my intent was to shake off cobwebs and write something by myself (with my friends’ help as available) that I could enjoy as a way to visit that old machine.  As an aside I wanted to learn some C++11 constructs including range-for, chrono, and lambdas.  I opened “apple2e.cpp” in vim on Saturday, November 5th, 2016, and made my first substantial push to Github implementing about 28 instructions around 1 AM that night.

I imagined that writing a huge table-based CPU emulator before executing even one instruction might have killed my interest, so I wrote the instruction decode switch basically “on-demand.”  If the while-loop read an instruction that I hadn’t implemented yet, the program would print the hex byte for the instruction and exit.  I think Lawrence suggested this based on his TRS-80 emulator project.

At first, I had to write a new case statement just to execute one more machine cycle.  The first five new instructions occurred right away in the execution sequence.  But pretty quickly the progression became geometric (possibly even exponential or tangential, see below).  The sixteenth instruction I implemented got the machine through 10 more cycles before I had to implement a new instruction.  The forty-second instruction got me through another 70 cycles.  The fifty-second instruction got me through 45000.  That kind of amplification of effort was pretty exciting!

Here’s a graph of the first instructions seen for the first time and how many instructions were executed before it.

```
D8 "CLD"               0 :
20 "JSR oper"          1 :
A0 "LDY #oper"         2 :
84 "STY oper"          3 :
60 "RTS"               4 :
A9 "LDA #oper"         6 :
85 "STA oper"          7 :
AD "LDA oper"          8 : *
F0 "BEQ oper"         12 : *
D0 "BNE oper"         17 : **
2C "BIT oper"         18 : **
08 "PHP"              19 : **
8D "STA oper"         20 : **
4C "JMP oper"         21 : **
88 "DEC"              23 : **
30 "BMI oper"         24 : ***
48 "PHA"              34 : ****
29 "AND #oper"        38 : ****
98 "TYA"              40 : *****
18 "CLC"              41 : *****
69 "ADC #oper"        42 : *****
A5 "LDA oper"         45 : *****
A4 "LDY oper"         47 : ******
CC "CPY oper"         48 : ******
ED "SBC oper"         52 : ******
B0 "BCS oper"         53 : ******
8C "STY oper"         55 : *******
AC "LDY oper"         56 : *******
4A "LSR A"            63 : ********
09 "ORA #oper"        65 : ********
68 "PLA"              67 : ********
90 "BCC oper"         69 : ********
0A "ASL A"            71 : *********
05 "ORA oper"         73 : *********
10 "BPL oper"         78 : *********
65 "ADC oper"         80 : **********
A8 "TAY"              84 : **********
B9 "LDA oper,Y"       87 : ***********
28 "PHP"             128 : ****************
A2 "LDX #oper"       136 : *****************
94 "STY oper,X"      141 : ******************
95 "STA oper,X"      142 : ******************
B4 "LDY oper,X"      216 : ***************************
C0 "CPY #oper"       218 : ***************************
B5 "LDA oper,X"      231 : *****************************
BD "LDA oper,X"      334 : ******************************************
DD "CMP oper,X"      335 : ******************************************
C9 "CMP #oper"       356 : *********************************************
EA "NOP"             366 : **********************************************
6C "JMP (oper)"      373 : ***********************************************
38 "SEC"             405 : ***************************************************
E9 "SBC #oper"       407 : ****************************************************
```

Pretty quickly I discovered that the ROM probes the hardware and uses hardware features before putting anything in the text display buffer.  Just like the instruction decode, I caught all access to the memory-mapped I/O region and dumped a message and exited if the CPU accessed unimplemented I/O.  I added switching between memory banks, ignored speaker access, and stubbed out display page switching.

The thing I was really waiting for was the "Apple //e" banner that the old computer printed within a second of booting.  On a 1 MHz 6502, one second would not have been more than a few hundred thousand instructions, so I kept my eye on that.

I added code to dump the region of memory which was scanned out as text any time that region was changed, and after adding a few more instructions I was able to watch the ROM clear the screen, and finally display “Apple //e” centered at the top.  The AppleSoft BASIC prompt “]” followed after a few more instructions and then the flashing checkerboard cursor.  As an aside, I didn’t throttle the CPU speed, and the cursor is CPU controlled, so the CPU was flashing madly, indicating the emulation was running many times the speed of the hardware!  Lawrence implemented raw keyboard terminal access as quickly as I implemented the keyboard strobe.  I added a rudimentary debugger so I could inspect my faulty CPU implementation.  After just a few more instructions I was able to enter and run simple BASIC programs.  Twenty-four hours had not yet elapsed since opening an empty source file.

## Challenge

Over the next week I ironed out a couple more bugs.  I used an open-source 6502 emulator as a reference and added code to run it in step with my CPU and point out where they differed.   (That felt a little like cheating, but was incredibly useful finding bugs.)   When I was able to run a handful of fun BASIC programs that used only text mode and no floppy, I announced to the usual suspects that I was done.

My brother, Bart, and my friend, Jim Miller, both joked that I should continue so they could run Choplifter and Lode Runner.  Those games would require floppy access, joysticks, audio, and high-resolution graphics, and I imagined those would be nearly impossible to implement in my spare time.

Still, the incomplete nature of the emulator kept nagging at me.  I continued to add new features in the same mode; I’d pick a new piece of old software, run it, and implement whatever was missing until it worked.

Over the course of November, I filled out the emulation into what I figure is about 80% of an Apple //e.  That loop and switch statement expanded to implement about 150 instructions.  I leveraged open source frameworks and modern hardware features.  I added low-resolution (“GR”) and high-resolution graphics (“HGR”) modes through OpenGL shaders and GLFW, allowing me to run graphics programs.  I added floppy hardware with borrowed code that made a floppy image look like the “nybbles” output by the “Disk ][“ floppy controller card and then ran some of my old programs from DOS and ProDOS floppies.  I straightened out muddled implementations of hardware features and added 80-column mode so I could dump ancient text files.  I added audio by simulating a speaker being toggled by a memory access and outputting a 44KHz stream through libao, and then I could run my own hybrid BASIC-assembly audio playback tool.  I added some UI widgets with the Apple ][ font.  I added joystick and paddle emulation using the GLFW gamepad functions, and finally the emulator supported ChopLifter and Lode Runner with graphics and sound.

In early December 2016, after one month of effort, we all sat around and played Apple ][ games in my living room.  Running 35-year-old programs over display mirroring is surprisingly fun, especially when a single row of pixels from those old programs is about 1/6th of an inch (half a centimeter) tall on my living room TV.

## Observations

I’ve known for a long time that I really struggle to stay engaged in a project when I’m not excited by it or at least curious about why something doesn’t work.

Lately, I’ve learned that one sure way for me to stay excited is to implement something that enables something else.  I’ve been involved in projects that have a long process of gathering requirements, architecting, and then implementing, and I fight to keep my attention in those instances.  In projects that right away start showing that they solve a problem, I work harder.  In particular, if a project has somewhat well-defined chunks of implementation, each of which enables some functionality and for which the next chunk is an interesting or fun challenge, I remain engaged.

In the last years, I’ve been involved in 3 projects like this.  Lawrence and I implemented the hardware and BIOS to support Z-80 CP/M 2.2.  Each little packet of work allowed us to run more and more old software (like the CP/M banner, then the command shell, then Microsoft BASIC, then Sargon Chess, and finally WordStar), which is really rewarding and encouraging.  We ported very old Silicon Graphics 3D workstation demos to modern MacOS including a software 3D engine.  Each additional demo required a few more features, after which we could see the results of a small amount of work in a very satisfying visual display.  Finally, this Apple //e emulator gave me lots of opportunities to implement a well-defined feature after which I could run yet more software.

These retrocomputing projects are just a hobby, but I used my projects to learn or practice skills for my resume.  For the Z-80 project I learned modern ARM microcontroller programming for I/O.  For the Apple //e emulator I learned C++11 lambdas (and abused them!).  I think it’s also very important to note that my peers challenge and encourage me to proceed.  It was great and I’d recommend implementing an emulator to any programmer wanting fun and practice.

