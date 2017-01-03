/*
 *
 * ao_example.c
 *
 *     Written by Stan Seibert - July 2001
 *
 * Legal Terms:
 *
 *     This source file is released into the public domain.  It is
 *     distributed without any warranty; without even the implied
 *     warranty * of merchantability or fitness for a particular
 *     purpose.
 *
 * Function:
 *
 *     This program opens the default driver and plays a 440 Hz tone for
 *     one second.
 *
 * Compilation command line (for Linux systems):
 *
 *     gcc -o ao_example ao_example.c -lao -ldl -lm
 *
 */

#include <stdio.h>
#include <string.h>
#include <ao/ao.h>
#include <math.h>

#define BUF_SIZE 4096

int main(int argc, char **argv)
{
        ao_device *device;
        ao_sample_format format;
        int default_driver;
        char *buffer;
        int buf_size;
        int sample;
        float freq = 440.0;
        int i;

        /* -- Initialize -- */

        fprintf(stderr, "libao example program\n");

        ao_initialize();

        /* -- Setup for default driver -- */

        default_driver = ao_default_driver_id();

        memset(&format, 0, sizeof(format));
        format.bits = 8;
        format.channels = 1;
        format.rate = 44100;
        format.byte_format = AO_FMT_LITTLE;

        /* -- Open driver -- */
        device = ao_open_live(default_driver, &format, NULL /* no options */);
        if (device == NULL) {
                fprintf(stderr, "Error opening device.\n");
                return 1;
        }

        /* -- Play some stuff -- */
        buf_size = format.bits/8 * format.channels * format.rate;
        buffer = (char*)calloc(buf_size,
                        sizeof(char));

        for (i = 0; i < format.rate; i++) {
                sample = 128 - (int)(0.75 * 128.0 *
                        sin(2 * M_PI * freq * ((float) i/format.rate)));

                /* Put the same stuff in left and right channel */
                buffer[i] = sample;
                printf("%d\n", sample);
        }
        ao_play(device, buffer, buf_size);

        /* -- Close and shutdown -- */
        ao_close(device);

        ao_shutdown();

  return (0);
}
