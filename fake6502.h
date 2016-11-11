#include <stdint.h>

extern "C" {

void reset6502();
void nmi6502();
void irq6502();
void exec6502(uint32_t tickcount);
void step6502();
void hookexternal(void *funcptr);

};
