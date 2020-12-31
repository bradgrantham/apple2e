#include <array>
#include <algorithm>
#include <cstdio>
#include "cpu6502.h"
#include "dis6502.h"

struct dummyclock
{
    int cycles = 0;
    void add_cpu_cycles(int N) {
        cycles += N;
    }
};

struct bus
{
    std::array<uint8_t, 64 * 1024> memory;
    bus()
    {
        std::fill(memory.begin(), memory.end(), 0xA5);
    }
    uint8_t read(uint16_t addr) const
    {
        printf("read 0x%04X yields 0x%02X\n", addr, memory[addr]);
        return memory[addr];
    }
    void write(uint16_t addr, uint8_t data)
    {
        printf("write 0x%02X to 0x%04X\n", data, addr);
        memory[addr] = data;
    }
};

template<class CLK, class BUS>
void print_cpu_state(const CPU6502<CLK, BUS>& cpu)
{
    printf("6502: A:%02X X:%02X Y:%02X P:", cpu.a, cpu.x, cpu.y);
    printf("%s", (cpu.p & cpu.N) ? "N" : "n");
    printf("%s", (cpu.p & cpu.V) ? "V" : "v");
    printf("-");
    printf("%s", (cpu.p & cpu.B) ? "B" : "b");
    printf("%s", (cpu.p & cpu.D) ? "D" : "d");
    printf("%s", (cpu.p & cpu.I) ? "I" : "i");
    printf("%s", (cpu.p & cpu.Z) ? "Z" : "z");
    printf("%s ", (cpu.p & cpu.C) ? "C" : "c");
    printf("S:%02X ", cpu.s);
    printf("PC:%04X\n", cpu.pc);
}

template<class BUS>
std::string read_bus_and_disassemble(const BUS &bus, int pc)
{
    uint8_t buf[4];
    buf[0] = bus.read(pc + 0);
    buf[1] = bus.read(pc + 1);
    buf[2] = bus.read(pc + 2);
    buf[3] = bus.read(pc + 3);
    auto [bytes, dis] = disassemble_6502(pc, buf);
    return dis;
}

int main(int argc, const char **argv)
{
    if(argc < 2) {
        fprintf(stderr, "usage: %s testfile.bin\n", argv[0]);
    }

    bus machine;

    FILE *testbin = fopen(argv[1], "rb");
    if(!testbin) {
        printf("couldn't open \"%s\" for reading\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    fseek(testbin, 0, SEEK_END);
    long length = ftell(testbin);
    fseek(testbin, 0, SEEK_SET);
    fread(machine.memory.data(), 1, length, testbin);
    fclose(testbin);

    dummyclock clock;

    CPU6502<dummyclock, bus> cpu(clock, machine);

    cpu.cycle();

    cpu.pc = 0x400;

    uint16_t oldpc = 0x0;
    do {
        print_cpu_state(cpu);
        printf("%s\n", read_bus_and_disassemble(machine, cpu.pc).c_str());
        oldpc = cpu.pc;
        cpu.cycle();
    } while(cpu.pc != oldpc);
    print_cpu_state(cpu);
    exit(EXIT_SUCCESS);
}
