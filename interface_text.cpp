#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <deque>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <map>

#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "interface.h"

using namespace std;

namespace APPLE2Einterface
{

bool textport_needs_output[2] = {false, false};
DisplayMode display_mode = TEXT;
int display_page = 0; // Apple //e page minus 1 (so 0,1 not 1,2)
bool mixed_mode = false;
bool vid80 = false;
bool altchar = false;

static const int text_page1_base = 0x400;
static const int text_page2_base = 0x800;
static const int text_page_size = 0x400;

unsigned char textport[2][24][40];

deque<event> event_queue;

bool event_waiting()
{
    return event_queue.size() > 0;
}

event dequeue_event()
{
    if(event_waiting()) {
        event e = event_queue.front();
        event_queue.pop_front();
        return e;
    } else
        return {NONE, 0};
}

tuple<float,bool> get_paddle(int num)
{
    if(num < 0 || num > 3)
        return make_tuple(-1, false);
    return make_tuple(0, false);
}

struct termios oldtermios;

static int ttyraw(int fd)
{
    /* Set terminal mode as follows:
       Noncanonical mode - turn off ICANON.
       Turn off signal-generation (ISIG)
       including BREAK character (BRKINT).
       Turn off any possible preprocessing of input (IEXTEN).
       Turn ECHO mode off.
       Disable CR-to-NL mapping on input.
       Disable input parity detection (INPCK).
       Disable stripping of eighth bit on input (ISTRIP).
       Disable flow control (IXON).
       Use eight bit characters (CS8).
       Disable parity checking (PARENB).
       Disable any implementation-dependent output processing (OPOST).
       One byte at a time input (MIN=1, TIME=0).
       */

    // Save old settings.
    struct termios newtermios;
    if (tcgetattr(fd, &oldtermios) < 0) {
        return -1;
    }
    newtermios = oldtermios;

    newtermios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* OK, why IEXTEN? If IEXTEN is on, the DISCARD character
       is recognized and is not passed to the process. This 
       character causes output to be suspended until another
       DISCARD is received. The DSUSP character for job control,
       the LNEXT character that removes any special meaning of
       the following character, the REPRINT character, and some
       others are also in this category.
       */

    newtermios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* If an input character arrives with the wrong parity, then INPCK
       is checked. If this flag is set, then IGNPAR is checked
       to see if input bytes with parity errors should be ignored.
       If it shouldn't be ignored, then PARMRK determines what
       character sequence the process will actually see.

       When we turn off IXON, the start and stop characters can be read.
       */

    newtermios.c_cflag &= ~(CSIZE | PARENB);
    /* CSIZE is a mask that determines the number of bits per byte.
       PARENB enables parity checking on input and parity generation
       on output.
       */

    newtermios.c_cflag |= CS8;
    /* Set 8 bits per character. */

    // newtermios.c_oflag &= ~(OPOST);
    /* This includes things like expanding tabs to spaces. */

    newtermios.c_cc[VMIN] = 1;
    newtermios.c_cc[VTIME] = 0;

    /* You tell me why TCSAFLUSH. */
    if (tcsetattr(fd, TCSAFLUSH, &newtermios) < 0) {
        return -1;
    }

    // Make the input non-blocking.
    fcntl(fd, F_SETFL, O_NONBLOCK);

    return 0;
}


int ttyreset(int fd)
{
    if (tcsetattr(fd, TCSAFLUSH, &oldtermios) < 0) {
        return -1;
    }

    // Make blocking.
    fcntl(fd, F_SETFL, 0);

    return 0;
}

void start_keyboard()
{
    // Set raw mode on stdin.
    if (ttyraw(0) < 0) {
        fprintf(stderr,"Can't go to raw mode.\n");
        exit(1);
    }
}

void stop_keyboard()
{
    if (ttyreset(0) < 0) {
        fprintf(stderr, "Cannot reset terminal!\n");
        exit(-1);
    }
}


void start(bool run_fast, bool add_floppies, bool floppy0_inserted, bool floppy1_inserted)
{
    event_queue.push_back({KEYDOWN, CAPS_LOCK});
    start_keyboard();
}

void apply_writes(void);

void poll_keyboard()
{
    int i;
    char c;

    while((i = read(0, &c, 1)) != -1) {
        bool control = false;
        int ch;
        if(c == '\r') {
            ch = ENTER;
        } else if(c >= 1 && c<= 26) {
            control = true;
            ch = 'A' + c - 1;
        } else if(c >= 'a' && c<= 'z') {
            ch = 'A' + c - 'a';
        } else {
            ch = c;
        }
        if(control)
            event_queue.push_back({KEYDOWN, LEFT_CONTROL});
        event_queue.push_back({KEYDOWN, ch});
        event_queue.push_back({KEYUP, ch});
        if(control)
            event_queue.push_back({KEYUP, LEFT_CONTROL});
    }
    if (errno == EAGAIN) {
        // Nothing to read.
    } else {
        printf("Got error reading from keyboard: %d\n\r", errno);
        exit(1);
    }
}

void iterate(const ModeHistory& history, unsigned long long current_byte_in_frame, float megahertz)
{
    apply_writes();

    if(false && textport_needs_output[display_page])
    {
        printf("\033[0;0H");
        printf(".----------------------------------------.\n");
        for(int row = 0; row < 24; row++) {
            putchar('|');
            for(int col = 0; col < 40; col++) {
                int ch = textport[display_page][row][col] & 0x7F;
                printf("%c", isprint(ch) ? ch : '?');
            }
            puts("|");
        }
        printf("`----------------------------------------'\n");
        textport_needs_output[display_page] = false;
    }

    poll_keyboard();
}

void shutdown()
{
    stop_keyboard();
}

void set_switches(DisplayMode mode_, bool mixed, int page, bool vid80_, bool altchar_)
{
    display_mode = mode_;
    mixed_mode = mixed;
    display_page = page;
    vid80 = vid80_;
    altchar = altchar_;

    // XXX
    static bool altchar_warned = false;
    if(altchar && !altchar_warned) {
        fprintf(stderr, "Warning: ALTCHAR activated, is not implemented\n");
        altchar_warned = true;
    }
}

extern int text_row_base_offsets[24];

map<int, unsigned char> writes;
int collisions = 0;

void write2(int addr, unsigned char data)
{
    // We know text page 1 and 2 are contiguous
    if((addr >= text_page1_base) && (addr < text_page2_base + text_page_size)) {
        int page = (addr >= text_page2_base) ? 1 : 0;
        int within_page = addr - text_page1_base - page * text_page_size;
        for(int row = 0; row < 24; row++) {
            int row_offset = text_row_base_offsets[row];
            if((within_page >= row_offset) && (within_page < row_offset + 40)) {
                int col = within_page - row_offset;
                textport[page][row][col] = data;
                textport_needs_output[page] = true;
            }
        }

    }
}

void apply_writes(void)
{
    for(auto it : writes) {
        int addr = it.first;
        write2(addr, it.second); 
    }
    writes.clear();
    collisions = 0;
}

bool write(int addr, bool aux, unsigned char data)
{
    // We know text page 1 and 2 are contiguous
    if((addr >= text_page1_base) && (addr < text_page2_base + text_page_size)) {

        if(writes.find(addr) != writes.end())
            collisions++;
        writes[addr] = data;
        return true;

    }
    return false;
}

int text_row_base_offsets[24] =
{
    0x000,
    0x080,
    0x100,
    0x180,
    0x200,
    0x280,
    0x300,
    0x380,
    0x028,
    0x0A8,
    0x128,
    0x1A8,
    0x228,
    0x2A8,
    0x328,
    0x3A8,
    0x050,
    0x0D0,
    0x150,
    0x1D0,
    0x250,
    0x2D0,
    0x350,
    0x3D0,
};


void show_floppy_activity(int number, bool activity)
{
}

void enqueue_audio_samples(char *buf, size_t sz)
{
}

};
