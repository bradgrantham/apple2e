#include <tuple>

struct event {
    enum Type {NONE, KEYDOWN, KEYUP, RESET, REBOOT, QUIT} type;
    static const int LEFT_SHIFT = 340;
    static const int LEFT_CONTROL = 341;
    static const int LEFT_ALT = 342;
    static const int LEFT_SUPER = 343;
    static const int RIGHT_SHIFT = 344;
    static const int RIGHT_CONTROL = 345;
    static const int RIGHT_ALT = 346;
    static const int RIGHT_SUPER = 347;
    static const int ESCAPE = 256;
    static const int ENTER = 257;
    static const int TAB = 258;
    static const int BACKSPACE = 259;
    static const int INSERT = 260;
    static const int DELETE = 261;
    static const int RIGHT = 262;
    static const int LEFT = 263;
    static const int DOWN = 264;
    static const int UP = 265;
    static const int PAGE_UP = 266;
    static const int PAGE_DOWN = 267;
    static const int HOME = 268;
    static const int END = 269;
    static const int CAPS_LOCK = 280;
    int value;
    event(Type type_, int value_) :
        type(type_),
        value(value_)
    {}
};

bool interface_event_waiting();
event interface_dequeue_event();
void interface_start();
void interface_iterate();
void interface_shutdown();
