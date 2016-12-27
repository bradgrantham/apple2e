#include <tuple>

namespace APPLE2Einterface
{
enum EventType {NONE, KEYDOWN, KEYUP, RESET, REBOOT, PASTE, SPEED, QUIT, PAUSE, EJECT_FLOPPY, INSERT_FLOPPY};

const int LEFT_SHIFT = 340;
const int LEFT_CONTROL = 341;
const int LEFT_ALT = 342;
const int LEFT_SUPER = 343;
const int RIGHT_SHIFT = 344;
const int RIGHT_CONTROL = 345;
const int RIGHT_ALT = 346;
const int RIGHT_SUPER = 347;
const int ESCAPE = 256;
const int ENTER = 257;
const int TAB = 258;
const int BACKSPACE = 259;
const int INSERT = 260;
const int DELETE = 261;
const int RIGHT = 262;
const int LEFT = 263;
const int DOWN = 264;
const int UP = 265;
const int PAGE_UP = 266;
const int PAGE_DOWN = 267;
const int HOME = 268;
const int END = 269;
const int CAPS_LOCK = 280;

struct event {
    EventType type;
    int value;
    char *str;
    event(EventType type_, int value_, char *str_ = NULL) :
        type(type_),
        value(value_),
        str(str_)
    {}
};

void start(bool run_fast, bool add_floppies, bool floppy0_inserted, bool floppy1_inserted);
void iterate(); // display
void shutdown();

bool event_waiting();
event dequeue_event();

enum DisplayMode {TEXT, LORES, HIRES};
void set_switches(DisplayMode mode, bool mixed, int page, bool vid80, bool altchar);
bool write(int addr, bool aux, unsigned char data);

std::tuple<float,bool> get_paddle(int num);

void show_floppy_activity(int number, bool activity);

void enqueue_audio_samples(char *buf, size_t sz);

};
