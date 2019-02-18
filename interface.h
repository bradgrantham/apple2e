#include <tuple>
#include <vector>

namespace APPLE2Einterface
{

enum EventType
{
    NONE, KEYDOWN, KEYUP, RESET, REBOOT, PASTE, SPEED, QUIT, PAUSE, EJECT_FLOPPY, INSERT_FLOPPY,
    REQUEST_ITERATION_PERIOD_IN_MILLIS,         /* request fixed simulation time period between calls to iterate() */
    WITHDRAW_ITERATION_PERIOD_REQUEST,          /* withdraw request for fixed simulation time */
};

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

bool event_waiting();
event dequeue_event();

enum DisplayMode {TEXT, LORES, HIRES};

struct ModeSettings
{
    DisplayMode mode;
    bool mixed;
    int page; // Apple //e page minus 1 (so 0,1 not 1,2)
    bool vid80;
    bool altchar;
    ModeSettings() :
        mode(TEXT),
        mixed(false),
        page(0),
        vid80(false),
        altchar(false)
    {}
    ModeSettings(DisplayMode mode_, bool mixed_, int page_, bool vid80_, bool altchar_) :
        mode(mode_),
        mixed(mixed_),
        page(page_),
        vid80(vid80_),
        altchar(altchar_)
    {}
    bool operator !=(const ModeSettings& ms)
    {
        return
            (ms.mode != mode) || 
            (ms.mixed != mixed) || 
            (ms.page != page) || 
            (ms.vid80 != vid80) || 
            (ms.altchar != altchar);
    }
};

typedef std::tuple<unsigned long long, ModeSettings> ModePoint;
typedef std::vector<ModePoint> ModeHistory;

bool write(int addr, bool aux, unsigned char data);

std::tuple<float,bool> get_paddle(int num);

void show_floppy_activity(int number, bool activity);

void enqueue_audio_samples(char *buf, size_t sz);

void start(bool run_fast, bool add_floppies, bool floppy0_inserted, bool floppy1_inserted);
void iterate(const ModeHistory& history, unsigned long long current_byte_in_frame, float megahertz); // display
void shutdown();

};
