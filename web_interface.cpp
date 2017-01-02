#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <deque>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <map>
#include <emscripten/bind.h>

#include "interface.h"

using namespace std;

namespace APPLE2Einterface
{

chrono::time_point<chrono::system_clock> start_time;

DisplayMode display_mode = TEXT;
int display_page = 0; // Apple //e page minus 1 (so 0,1 not 1,2)
bool mixed_mode = false;
bool vid80 = false;
bool altchar = false;

bool use_joystick = false;

deque<event> event_queue;

bool force_caps_on = true;

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

float paddle_values[4] = {0, 0, 0, 0};
bool paddle_buttons[4] = {false, false, false, false};

tuple<float,bool> get_paddle(int num)
{
    if(num < 0 || num > 3)
        make_tuple(-1, false);
    return make_tuple(paddle_values[num], paddle_buttons[num]);
}

void enqueue_audio_samples(char *buf, size_t sz)
{
}

void start(bool run_fast, bool add_floppies, bool floppy0_inserted, bool floppy1_inserted)
{
    start_time = std::chrono::system_clock::now();
}

void apply_writes(void);

bool textport_changed = false;
unsigned char textport[2][24][40];

void iterate()
{
    apply_writes();

    if(textport_changed) {
        printf("------------------------------------------\n");
        for(int row = 0; row < 24; row++) {
            printf("|");
            for(int col = 0; col < 40; col++) {
                char ch = textport[display_page][row][col] & 0x7f;
                putchar(isprint(ch) ? ch : '?');
            }
            printf("|\n");
        }
        printf("------------------------------------------\n");
        textport_changed = false;
    }
}

void shutdown()
{
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

static const int text_page1_base = 0x400;
static const int text_page2_base = 0x800;
static const int text_page_size = 0x400;
static const int hires_page1_base = 0x2000;
static const int hires_page2_base = 0x4000;
static const int hires_page_size = 8192;

extern int text_row_base_offsets[24];
extern int hires_memory_to_scanout_address[8192];

map< tuple<int, bool>, unsigned char> writes;
int collisions = 0;

void write2(int addr, bool aux, unsigned char data)
{
    // We know text page 1 and 2 are contiguous
    if((addr >= text_page1_base) && (addr < text_page2_base + text_page_size)) {
        int page = (addr >= text_page2_base) ? 1 : 0;
        int within_page = addr - text_page1_base - page * text_page_size;
        for(int row = 0; row < 24; row++) {
            int row_offset = text_row_base_offsets[row];
            if((within_page >= row_offset) && (within_page < row_offset + 40)) {
                int col = within_page - row_offset;
                if(!aux) textport[page][row][col] = data;
            }
        }

    } else if(((addr >= hires_page1_base) && (addr < hires_page1_base + hires_page_size)) || ((addr >= hires_page2_base) && (addr < hires_page2_base + hires_page_size))) {

        int page = (addr < hires_page2_base) ? 0 : 1;
        int page_base = (page == 0) ? hires_page1_base : hires_page2_base;
        int within_page = addr - page_base;
        int scanout_address = hires_memory_to_scanout_address[within_page];
        int row = scanout_address / 40;
        int col = scanout_address % 40;
    }
}

void apply_writes(void)
{
    for(auto it : writes) {
        int addr;
        bool aux;
        tie(addr, aux) = it.first;
        write2(addr, aux, it.second); 
        textport_changed = true;
    }
    writes.clear();
    collisions = 0;
}

bool write(int addr, bool aux, unsigned char data)
{
    // We know text page 1 and 2 are contiguous
    if((addr >= text_page1_base) && (addr < text_page2_base + text_page_size)) {

        if(writes.find(make_tuple(addr, aux)) != writes.end())
            collisions++;
        writes[make_tuple(addr, aux)] = data;
        return true;

    } else if(((addr >= hires_page1_base) && (addr < hires_page1_base + hires_page_size)) || ((addr >= hires_page2_base) && (addr < hires_page2_base + hires_page_size))) {

        if(writes.find(make_tuple(addr, aux)) != writes.end())
            collisions++;
        writes[make_tuple(addr, aux)] = data;
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

int hires_row_base_offsets[192] =
{
     0x0000,  0x0400,  0x0800,  0x0C00,  0x1000,  0x1400,  0x1800,  0x1C00, 
     0x0080,  0x0480,  0x0880,  0x0C80,  0x1080,  0x1480,  0x1880,  0x1C80, 
     0x0100,  0x0500,  0x0900,  0x0D00,  0x1100,  0x1500,  0x1900,  0x1D00, 
     0x0180,  0x0580,  0x0980,  0x0D80,  0x1180,  0x1580,  0x1980,  0x1D80, 
     0x0200,  0x0600,  0x0A00,  0x0E00,  0x1200,  0x1600,  0x1A00,  0x1E00, 
     0x0280,  0x0680,  0x0A80,  0x0E80,  0x1280,  0x1680,  0x1A80,  0x1E80, 
     0x0300,  0x0700,  0x0B00,  0x0F00,  0x1300,  0x1700,  0x1B00,  0x1F00, 
     0x0380,  0x0780,  0x0B80,  0x0F80,  0x1380,  0x1780,  0x1B80,  0x1F80, 
     0x0028,  0x0428,  0x0828,  0x0C28,  0x1028,  0x1428,  0x1828,  0x1C28, 
     0x00A8,  0x04A8,  0x08A8,  0x0CA8,  0x10A8,  0x14A8,  0x18A8,  0x1CA8, 
     0x0128,  0x0528,  0x0928,  0x0D28,  0x1128,  0x1528,  0x1928,  0x1D28, 
     0x01A8,  0x05A8,  0x09A8,  0x0DA8,  0x11A8,  0x15A8,  0x19A8,  0x1DA8, 
     0x0228,  0x0628,  0x0A28,  0x0E28,  0x1228,  0x1628,  0x1A28,  0x1E28, 
     0x02A8,  0x06A8,  0x0AA8,  0x0EA8,  0x12A8,  0x16A8,  0x1AA8,  0x1EA8, 
     0x0328,  0x0728,  0x0B28,  0x0F28,  0x1328,  0x1728,  0x1B28,  0x1F28, 
     0x03A8,  0x07A8,  0x0BA8,  0x0FA8,  0x13A8,  0x17A8,  0x1BA8,  0x1FA8, 
     0x0050,  0x0450,  0x0850,  0x0C50,  0x1050,  0x1450,  0x1850,  0x1C50, 
     0x00D0,  0x04D0,  0x08D0,  0x0CD0,  0x10D0,  0x14D0,  0x18D0,  0x1CD0, 
     0x0150,  0x0550,  0x0950,  0x0D50,  0x1150,  0x1550,  0x1950,  0x1D50, 
     0x01D0,  0x05D0,  0x09D0,  0x0DD0,  0x11D0,  0x15D0,  0x19D0,  0x1DD0, 
     0x0250,  0x0650,  0x0A50,  0x0E50,  0x1250,  0x1650,  0x1A50,  0x1E50, 
     0x02D0,  0x06D0,  0x0AD0,  0x0ED0,  0x12D0,  0x16D0,  0x1AD0,  0x1ED0, 
     0x0350,  0x0750,  0x0B50,  0x0F50,  0x1350,  0x1750,  0x1B50,  0x1F50, 
     0x03D0,  0x07D0,  0x0BD0,  0x0FD0,  0x13D0,  0x17D0,  0x1BD0,  0x1FD0, 
};

int hires_memory_to_scanout_address[8192];

static void initialize_memory_to_scanout() __attribute__((constructor));
void initialize_memory_to_scanout()
{
    for(int row = 0; row < 192; row++) {
        int row_address = hires_row_base_offsets[row];
        for(int byte = 0; byte < 40; byte++) {
            hires_memory_to_scanout_address[row_address + byte] = row * 40 + byte;
        }
    }
}

void show_floppy_activity(int number, bool activity)
{
}

void enqueue_console_keydown(int key)
{
    static bool caps_lock_down = false;

    // XXX not ideal, can be enqueued out of turn
    if(caps_lock_down && !force_caps_on) {
        caps_lock_down = false;
        event_queue.push_back({KEYUP, CAPS_LOCK});
    } else if(!caps_lock_down && force_caps_on) {
        caps_lock_down = true;
        event_queue.push_back({KEYDOWN, CAPS_LOCK});
    }

    switch(key) {
        case 16: key = LEFT_SHIFT; break;
        case 13: key = ENTER; break;
        case 37: key = LEFT; break;
        case 186: key = ';'; break;
        case 187: key = '='; break;
        case 188: key = ','; break;
        case 189: key = '-'; break;
        case 190: key = '.'; break;
        case 191: key = '/'; break;
        case 219: key = '['; break;
        case 221: key = ']'; break;
        case 222: key = '\''; break;
    }

    if(force_caps_on && (key >= 'a') && (key <= 'z'))
        key = key - 'a' + 'A';

    event_queue.push_back({KEYDOWN, key});
}

void enqueue_console_keyup(int key)
{
    static bool caps_lock_down = false;

    // XXX not ideal, can be enqueued out of turn
    if(caps_lock_down && !force_caps_on) {
        caps_lock_down = false;
        event_queue.push_back({KEYUP, CAPS_LOCK});
    } else if(!caps_lock_down && force_caps_on) {
        caps_lock_down = true;
        event_queue.push_back({KEYDOWN, CAPS_LOCK});
    }

    switch(key) {
        case 16: key = LEFT_SHIFT; break;
        case 13: key = ENTER; break;
        case 37: key = LEFT; break;
        case 186: key = ';'; break;
        case 187: key = '='; break;
        case 188: key = ','; break;
        case 189: key = '-'; break;
        case 190: key = '.'; break;
        case 191: key = '/'; break;
        case 219: key = '['; break;
        case 221: key = ']'; break;
        case 222: key = '\''; break;
    }

    if(force_caps_on && (key >= 'a') && (key <= 'z'))
        key = key - 'a' + 'A';

    event_queue.push_back({KEYUP, key});
}

EMSCRIPTEN_BINDINGS(my_module) {
    emscripten::function("enqueue_console_keydown", &enqueue_console_keydown);
    emscripten::function("enqueue_console_keyup", &enqueue_console_keyup);
}


};


