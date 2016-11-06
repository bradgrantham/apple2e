#ifndef _SIMULATOR_H_
#define _SIMULATOR_H_

#include <vector>
#undef max

struct board_base
{
    virtual bool write(int addr, unsigned char data) { return false; }
    virtual bool read(int addr, unsigned char &data) { return false; }
    virtual bool board_get_interrupt(int& irq) { return false; }

    virtual void reset(void) {}
    virtual void idle(void) {};
    virtual void pause(void) {};
    virtual void resume(void) {};
};

extern std::vector<board_base*> boards;

#endif /* _SIMULATOR_H_ */
