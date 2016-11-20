
#include <stdio.h>
#include <signal.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "keyboard.h"

static unsigned char last_key = 0;
static bool strobe = false;

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

unsigned char get_keyboard_data_and_strobe()
{
    return last_key | (strobe ? 0x80 : 0x00);
}

unsigned char get_any_key_down_and_clear_strobe()
{
    strobe = false;

    // Pretend that no keys are ever down right now.
    return 0x00;
}

bool started = false;

void start_keyboard()
{
    // Set raw mode on stdin.
    if (ttyraw(0) < 0) {
        fprintf(stderr,"Can't go to raw mode. Oh, well!\n");
        // exit(1);
        return;
    }
    started = true;
}

void stop_keyboard()
{
    if (started && (ttyreset(0) < 0)) {
        fprintf(stderr, "Cannot reset terminal!\n");
        exit(-1);
    }
    started = false;
}

bool peek_key(char *k)
{
    if(strobe)
        *k = last_key;
    return strobe;
}

void clear_strobe()
{
    strobe = false;
}

void poll_keyboard()
{
    int i;
    char c;

    if(!started)
        return;

    i = read(0, &c, 1);
    if (i == -1) {
        if (errno == EAGAIN) {
            // Nothing to read.
        } else {
            printf("Got error reading from keyboard: %d\n\r", errno);
            exit(1);
        }
    } else {
        last_key = c;
        strobe = true;
    }
}
