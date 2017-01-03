// g++ -Wall     `sdl-config --cflags --libs` testsdl.cpp -o testsdl

#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <SDL.h>

#define BUF_SIZE 4096

unsigned long long audio_pos;
unsigned char buf[32768];

const int freq = 440;
const int rate = 44100;

/* The audio function callback takes the following parameters:
   stream:  A pointer to the audio buffer to be filled
   len:     The length (in bytes) of the audio buffer
*/
void fill_audio(void *udata, Uint8 *stream, int len)
{
    for (int i = 0; i < len; i++) {
        buf[i] = (int)(0.75 * 128.0 *
            sin(2 * M_PI * freq * ((float) (audio_pos + i)/rate)));
    }

    /* Mix as much data as possible */
    SDL_MixAudio(stream, buf, len, SDL_MIX_MAXVOLUME);
    audio_pos += len;
}

extern "C" int main(int argc, char **argv)
{
    SDL_AudioSpec wanted;

    /* Set the audio format */
    wanted.freq = rate;
    wanted.format = AUDIO_S8;
    wanted.channels = 1;    /* 1 = mono, 2 = stereo */
    wanted.samples = 1024;  /* Good low-latency value for callback */
    wanted.callback = fill_audio;
    wanted.userdata = NULL;

    /* Open the audio device, forcing the desired format */
    if ( SDL_OpenAudio(&wanted, NULL) < 0 ) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        exit(1);
    }

    audio_pos = 0;

    /* Let the callback function play the audio chunk */
    SDL_PauseAudio(0);

    /* Wait for sound to complete */
    time_t now = time(0);
    while ( time(0) - now < 10 ) {
        SDL_Delay(100);         /* Sleep 1/10 second */
    }
    SDL_CloseAudio();

    return 0;
}
