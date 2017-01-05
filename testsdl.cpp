// g++ -Wall `sdl-config --cflags --libs` testsdl.cpp -o testsdl
// emcc -s USE_SDL=2 -Wall testsdl.cpp -o testsdl.html

#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <SDL.h>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#define BUF_SIZE 4096

unsigned long long audio_pos = 0;
unsigned short buf[2048];

float freq1 = 440;
float freq2 = 410;
const int rate = 44100;

/* The audio function callback takes the following parameters:
   stream:  A pointer to the audio buffer to be filled
   len:     The length (in bytes) of the audio buffer
*/
void fill_audio(void *userdata, Uint8 *stream, int len)
{
    printf("fill audio %d\n", len);
    for (int i = 0; i < len / 4; i++) {
        buf[i * 2 + 0] = (int)(0.75 * 32768.0 *
            sin(2 * M_PI * freq1 * ((float) (audio_pos + i)/rate)));
        buf[i * 2 + 1] = (int)(0.75 * 32768.0 *
            sin(2 * M_PI * freq2 * ((float) (audio_pos + i)/rate)));
    }

    /* Mix as much data as possible */
    SDL_MixAudio(stream, (unsigned char *)buf, len, SDL_MIX_MAXVOLUME);
    audio_pos += len / 4;
}

time_t now;
bool done = false;

void mainloop(void) 
{
    if ( time(0) - now >= 10 )
#ifdef EMSCRIPTEN
        emscripten_cancel_main_loop();
#else
        done = true;
#endif
}

extern "C" int main(int argc, char **argv)
{
    printf("main()\n");
    SDL_AudioSpec wanted;

    /* Set the audio format */
    wanted.freq = rate;
    wanted.format = AUDIO_S16;
    wanted.channels = 2;    /* 1 = mono, 2 = stereo */
    wanted.samples = 1024;  /* Good low-latency value for callback */
    wanted.callback = fill_audio;
    wanted.userdata = NULL;

    /* Open the audio device, forcing the desired format */
    if ( SDL_OpenAudio(&wanted, NULL) < 0 ) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        exit(1);
    }
    printf("opened audio\n");

    /* Let the callback function play the audio chunk */
    SDL_PauseAudio(0);
    printf("unpaused audio, playing...\n");

    now = time(0);
#ifdef EMSCRIPTEN
    emscripten_set_main_loop(mainloop, 10, true);
#else
    while(!done) {
        mainloop();
        SDL_Delay(100);
    }
#endif

    SDL_CloseAudio();
    printf("closing...\n");

    return 0;
}
