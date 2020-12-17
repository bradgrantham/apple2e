/*
    Public methods:
        CPU6502(CLK& clk, BUS& bus); - construct using clk and bus
        cycle() - issue one instruction and add necessary cycles to clk
        reset() - reset CPU state
        irq() - put CPU in IRQ
        nmi() - put CPU in NMI

    CLK template parameter must provide methods:
        void add_cpu_cycles(int N); - add N CPU cycles to the clock

    BUS template parameter must provide methods:
        uint8_t read(uint16_t addr);
        void read(uint16_t addr, uint8_t data);
*/

/*
  Candidates for refactor
      effective address
      carry flag from BCD and !BCD addition
*/

#ifndef CPU6502_H
#define CPU6502_H

#include <stdlib.h>
#include <assert.h>

#define SUPPORT_65C02 1

template<class CLK, class BUS>
struct CPU6502
{
    CLK &clk;
    BUS &bus;

    const static int32_t cycles[256];

    static constexpr uint8_t N = 0x80;
    static constexpr uint8_t V = 0x40;
    static constexpr uint8_t B = 0x10;
    static constexpr uint8_t D = 0x08;
    static constexpr uint8_t I = 0x04;
    static constexpr uint8_t Z = 0x02;
    static constexpr uint8_t C = 0x01;
    uint8_t a, x, y, s, p;
    uint16_t pc = 0;

    enum Exception {
        NONE,
        RESET,
        NMI,
        BRK,
        INT,
    } exception;

    void stack_push(uint8_t d)
    {
        bus.write(0x100 + s--, d);
    }

    uint8_t stack_pull()
    {
        return bus.read(0x100 + ++s);
    }

    uint8_t read_pc_inc()
    {
        return bus.read(pc++);
    }

    void flag_change(uint8_t flag, bool v)
    {
        if(v) {
            p |= flag;
        } else {
            p &= ~flag;
        }
    }

    void flag_set(uint8_t flag)
    {
        p |= flag;
    }

    void flag_clear(uint8_t flag)
    {
        p &= ~flag;
    }

    uint8_t carry()
    {
        return (p & C) ? 1 : 0;
    }

    bool isset(uint8_t flag)
    {
        return (p & flag) != 0;
    }

    void set_flags(uint8_t flags, uint8_t v)
    {
        if(flags & Z) {
            flag_change(Z, v == 0x00);
        }
        if(flags & N) {
            flag_change(N, v & 0x80);
        }
    }

    static bool sbc_overflow_d(uint8_t a, uint8_t b, uint8_t borrow)
    {
        int8_t a_ = a;
        int8_t b_ = b;
        int16_t c = a_ - (b_ + borrow);
        return (c < 0) || (c > 99);
    }

    static bool adc_overflow_d(uint8_t a, uint8_t b, uint8_t carry)
    {
        int8_t a_ = a;
        int8_t b_ = b;
        int16_t c = a_ + b_ + carry;
        return (c < 0) || (c > 99);
    }

    static bool sbc_overflow(uint8_t a, uint8_t b, uint8_t borrow)
    {
        int8_t a_ = a;
        int8_t b_ = b;
        int16_t c = a_ - (b_ + borrow);
        return (c < -128) || (c > 127);
    }

    static bool adc_overflow(uint8_t a, uint8_t b, uint8_t carry)
    {
        int8_t a_ = a;
        int8_t b_ = b;
        int16_t c = a_ + b_ + carry;
        return (c < -128) || (c > 127);
    }

    CPU6502(CLK& clk_, BUS& bus_) :
        clk(clk_),
        bus(bus_),
        a(0),
        x(0),
        y(0),
        s(0),
        p(0x20),
        exception(RESET)
    {
    }

    void reset()
    {
        s = 0xFD;
        uint8_t low = bus.read(0xFFFC);
        uint8_t high = bus.read(0xFFFD);
        pc = low + high * 256;
        exception = NONE;
    }

    void irq()
    {
        stack_push((pc - 1) >> 8);
        stack_push((pc - 1) & 0xFF);
        stack_push(p);
        uint8_t low = bus.read(0xFFFE);
        uint8_t high = bus.read(0xFFFF);
        pc = low + high * 256;
        exception = NONE;
    }

    void nmi()
    {
        stack_push((pc - 1) >> 8);
        stack_push((pc - 1) & 0xFF);
        stack_push(p);
        uint8_t low = bus.read(0xFFFA);
        uint8_t high = bus.read(0xFFFB);
        pc = low + high * 256;
        exception = NONE;
    }

    void cycle()
    {
        if(exception == RESET) {
            reset();
        } if(exception == NMI) {
            nmi();
        } if(exception == INT) {
            irq();
        }
        // BRK is a special case caused directly by an instruction

        uint8_t inst = read_pc_inc();

        uint8_t m;

        switch(inst) {
            case 0x00: { // BRK
                stack_push((pc - 1) >> 8);
                stack_push((pc - 1) & 0xFF);
                stack_push(p | B); // | B says the Synertek 6502 reference
                uint8_t low = bus.read(0xFFFE);
                uint8_t high = bus.read(0xFFFF);
                pc = low + high * 256;
                exception = NONE;
                break;
            }

            case 0xEA: { // NOP
                break;
            }

            case 0x8A: { // TXA
                set_flags(N | Z, a = x);
                break;
            }

            case 0xAA: { // TAX
                set_flags(N | Z, x = a);
                break;
            }

            case 0xBA: { // TSX
                set_flags(N | Z, x = s);
                break;
            }

            case 0x9A: { // TXS
                s = x;
                break;
            }

            case 0xA8: { // TAY
                set_flags(N | Z, y = a);
                break;
            }

            case 0x98: { // TYA
                set_flags(N | Z, a = y);
                break;
            }

            case 0x18: { // CLC
                flag_clear(C);
                break;
            }

            case 0x38: { // SEC
                flag_set(C);
                break;
            }

            case 0xF8: { // SED
                flag_set(D);
                break;
            }

            case 0xD8: { // CLD
                flag_clear(D);
                break;
            }

            case 0x58: { // CLI
                flag_clear(I);
                break;
            }

            case 0x78: { // SEI
                flag_set(I);
                break;
            }

            case 0xB8: { // CLV
                flag_clear(V);
                break;
            }

            case 0xC6: { // DEC zpg
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, m = bus.read(zpg) - 1);
                bus.write(zpg, m);
                break;
            }

            case 0xDE: { // DEC abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                set_flags(N | Z, m = bus.read(addr) - 1);
                bus.write(addr, m);
                break;
            }

            case 0xCE: { // DEC abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, m = bus.read(addr) - 1);
                bus.write(addr, m);
                break;
            }

            case 0xCA: { // DEX
                set_flags(N | Z, x = x - 1);
                break;
            }

            case 0xFE: { // INC abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                if((addr - x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, m = bus.read(addr) + 1);
                bus.write(addr, m);
                break;
            }

            case 0xEE: { // INC abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, m = bus.read(addr) + 1);
                bus.write(addr, m);
                break;
            }

            case 0xE6: { // INC zpg
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, m = bus.read(zpg) + 1);
                bus.write(zpg, m);
                break;
            }

            case 0xF6: { // INC zpg, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                set_flags(N | Z, m = bus.read(zpg) + 1);
                bus.write(zpg, m);
                break;
            }

            case 0xE8: { // INX
                set_flags(N | Z, x = x + 1);
                break;
            }

            case 0xC8: { // INY
                set_flags(N | Z, y = y + 1);
                break;
            }

            case 0x10: { // BPL
                int32_t rel = (read_pc_inc() + 128) % 256 - 128;
                if(!isset(N)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0x50: { // BVC
                int32_t rel = (read_pc_inc() + 128) % 256 - 128;
                if(!isset(V)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0x70: { // BVS
                int32_t rel = (read_pc_inc() + 128) % 256 - 128;
                if(isset(V)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0x30: { // BMI
                int32_t rel = (read_pc_inc() + 128) % 256 - 128;
                if(isset(N)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0x90: { // BCC
                int32_t rel = (read_pc_inc() + 128) % 256 - 128;
                if(!isset(C)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0xB0: { // BCS
                int32_t rel = (read_pc_inc() + 128) % 256 - 128;
                if(isset(C)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0xD0: { // BNE
                int32_t rel = (read_pc_inc() + 128) % 256 - 128;
                if(!isset(Z)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0xF0: { // BEQ
                int32_t rel = (read_pc_inc() + 128) % 256 - 128;
                if(isset(Z)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }


            case 0xA1: { // LDA (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xB5: { // LDA zpg, X
                uint8_t zpg = read_pc_inc();
                uint16_t addr = zpg + x;
                set_flags(N | Z, a = bus.read(addr & 0xFF));
                break;
            }

            case 0xB1: { // LDA ind, Y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xA5: { // LDA zpg
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, a = bus.read(zpg));
                break;
            }

            case 0xDD: { // CMP abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + x);
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xC1: { // CMP (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xD9: { // CMP abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + y);
                if((addr + y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xB9: { // LDA abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = bus.read(addr + y));
                if((addr + y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                break;
            }

            case 0xBC: { // LDY abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, y = bus.read(addr + x));
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                break;
            }

            case 0xBD: { // LDA abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = bus.read(addr + x));
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                break;
            }

            case 0xF5: { // SBC zpg, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, !(bcd <  m + borrow));
                    flag_change(V, sbc_overflow_d(bcd, m, borrow));
                    set_flags(N | Z, bcd = bcd - (m + borrow));
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xE5: { // SBC zpg
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, !(bcd <  m + borrow));
                    flag_change(V, sbc_overflow_d(bcd, m, borrow));
                    set_flags(N | Z, bcd = bcd - (m + borrow));
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xF1: { // SBC ind, Y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, !(bcd <  m + borrow));
                    flag_change(V, sbc_overflow_d(bcd, m, borrow));
                    set_flags(N | Z, bcd = bcd - (m + borrow));
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xF9: { // SBC abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                uint8_t m = bus.read(addr);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, !(bcd <  m + borrow));
                    flag_change(V, sbc_overflow_d(bcd, m, borrow));
                    set_flags(N | Z, bcd = bcd - (m + borrow));
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xFD: { // SBC abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                if((addr - x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                uint8_t m = bus.read(addr);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, !(bcd <  m + borrow));
                    flag_change(V, sbc_overflow_d(bcd, m, borrow));
                    set_flags(N | Z, bcd = bcd - (m + borrow));
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xED: { // SBC abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                uint8_t m = bus.read(addr);
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, !(bcd <  m + borrow));
                    flag_change(V, sbc_overflow_d(bcd, m, borrow));
                    set_flags(N | Z, bcd = bcd - (m + borrow));
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0xE9: { // SBC imm
                uint8_t m = read_pc_inc();
                uint8_t borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, !(bcd <  m + borrow));
                    flag_change(V, sbc_overflow_d(bcd, m, borrow));
                    set_flags(N | Z, bcd = bcd - (m + borrow));
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, !(a < (m + borrow)));
                    flag_change(V, sbc_overflow(a, m, borrow));
                    set_flags(N | Z, a = a - (m + borrow));
                }
                break;
            }

            case 0x71: { // ADC (ind), Y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, ((uint16_t)bcd + (uint16_t)m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x6D: { // ADC abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, ((uint16_t)bcd + (uint16_t)m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x65: { // ADC
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, ((uint16_t)bcd + (uint16_t)m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x7D: { // ADC abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + x;
                if((addr - x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, ((uint16_t)bcd + (uint16_t)m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x79: { // ADC abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, ((uint16_t)bcd + (uint16_t)m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x69: { // ADC
                m = read_pc_inc();
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, ((uint16_t)bcd + (uint16_t)m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x0E: { // ASL abs
                uint16_t addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = m << 1);
                bus.write(addr, m);
                break;
            }

            case 0x06: { // ASL
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = m << 1);
                bus.write(zpg, m);
                break;
            }

            case 0x16: { // ASL
                uint8_t zpg = read_pc_inc();
                m = bus.read((zpg + x) & 0xFF);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = m << 1);
                bus.write((zpg + x) & 0xFF, m);
                break;
            }

            case 0x0A: { // ASL
                flag_change(C, a & 0x80);
                set_flags(N | Z, a = a << 1);
                break;
            }

            case 0x5E: { // LSR abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + x);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                bus.write(addr + x, m);
                break;
            }

            case 0x46: { // LSR
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                bus.write(zpg, m);
                break;
            }

            case 0x56: { // LSR zpg, X
                uint8_t zpg = read_pc_inc() + x;
                m = bus.read(zpg & 0xFF);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                bus.write(zpg, m);
                break;
            }

            case 0x4E: { // LSR
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                bus.write(addr, m);
                break;
            }

            case 0x4A: { // LSR
                flag_change(C, a & 0x01);
                set_flags(N | Z, a = a >> 1);
                break;
            }

            case 0x68: { // PLA
                set_flags(N | Z, a = stack_pull());
                break;
            }

            case 0x48: { // PHA
                stack_push(a);
                break;
            }

            case 0x01: { // ORA (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x15: { // ORA zpg, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x0D: { // ORA abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x19: { // ORA abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + y);
                if((addr + y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x1D: { // ORA abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + x);
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x11: { // ORA (ind), Y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x05: { // ORA zpg
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x09: { // ORA imm
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, a = a | imm);
                break;
            }

            case 0x35: { // AND zpg, X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                set_flags(N | Z, a = a & bus.read(zpg));
                break;
            }

            case 0x31: { // AND (ind), y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = a & bus.read(addr));
                break;
            }

            case 0x3D: { // AND abs, x
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = a & bus.read(addr + x));
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                break;
            }

            case 0x39: { // AND abs, y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = a & bus.read(addr + y));
                if((addr + y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                break;
            }

            case 0x2D: { // AND abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = a & bus.read(addr));
                break;
            }

            case 0x25: { // AND zpg
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, a = a & bus.read(zpg));
                break;
            }

            case 0x29: { // AND imm
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, a = a & imm);
                break;
            }

            case 0x88: { // DEY
                set_flags(N | Z, y = y - 1);
                break;
            }

            case 0x7E: { // ROR abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + x);
                bool c = isset(C);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                bus.write(addr + x, m);
                break;
            }

            case 0x36: { // ROL zpg,X
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                bus.write(zpg, m);
                break;
            }


            case 0x3E: { // ROL abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + x);
                bool c = isset(C);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                bus.write(addr + x, m);
                break;
            }

            case 0x2A: { // ROL
                bool c = isset(C);
                flag_change(C, a & 0x80);
                set_flags(N | Z, a = (c ? 0x01 : 0x00) | (a << 1));
                break;
            }

            case 0x6A: { // ROR
                bool c = isset(C);
                flag_change(C, a & 0x01);
                set_flags(N | Z, a = (c ? 0x80 : 0x00) | (a >> 1));
                break;
            }

            case 0x6E: { // ROR abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                bus.write(addr, m);
                break;
            }

            case 0x66: { // ROR
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                bus.write(zpg, m);
                break;
            }

            case 0x76: { // ROR
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                bus.write(zpg, m);
                break;
            }

            case 0x2E: { // ROL abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                bool c = isset(C);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                bus.write(addr, m);
                break;
            }


            case 0x26: { // ROL
                uint8_t zpg = read_pc_inc();
                bool c = isset(C);
                m = bus.read(zpg);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                bus.write(zpg, m);
                break;
            }

            case 0x4C: { // JMP
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                pc = addr;
                break;
            }

            case 0x6C: { // JMP indirect
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                uint8_t addrl = bus.read(addr);
                uint8_t addrh = bus.read(addr + 1);
                addr = addrl + addrh * 256;
                pc = addr;
                break;
            }

            case 0x9D: { // STA abs, x
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                bus.write(addr + x, a);
                break;
            }

            case 0x99: { // STA
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                bus.write(addr + y, a);
                break;
            }

            case 0x91: { // STA (ind), Y
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                bus.write(addr, a);
                break;
            }

            case 0x81: { // STA (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                bus.write(addr, a);
                break;
            }

            case 0x8D: { // STA
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                bus.write(addr, a);
                break;
            }

            case 0x08: { // PHP
                stack_push(p);
                break;
            }

            case 0x28: { // PLP
                p = stack_pull();
                break;
            }

            case 0x24: { // BIT
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(Z, (a & m) == 0);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x40);
                break;
            }

            case 0x2C: { // BIT
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(Z, (a & m) == 0);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x40);
                break;
            }

            case 0xB4: { // LDY
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, y = bus.read((zpg + x) & 0xFF));
                break;
            }

            case 0xAE: { // LDX abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, x = bus.read(addr));
                break;
            }

            case 0xBE: { // LDX
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, x = bus.read(addr));
                break;
            }

            case 0xA6: { // LDX
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, x = bus.read(zpg));
                break;
            }

            case 0xB6: { // LDX zpg, Y
                uint8_t zpg = (read_pc_inc() + y) & 0xFF;
                set_flags(N | Z, x = bus.read(zpg));
                break;
            }

            case 0xA4: { // LDY
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, y = bus.read(zpg));
                break;
            }

            case 0xAC: { // LDY
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, y = bus.read(addr));
                break;
            }

            case 0xA2: { // LDX
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, x = imm);
                break;
            }

            case 0xA0: { // LDY
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, y = imm);
                break;
            }

            case 0xA9: { // LDA
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, a = imm);
                break;
            }

            case 0xAD: { // LDA
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xCC: { // CPY abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m <= y);
                set_flags(N | Z, m = y - m);
                break;
            }

            case 0xEC: { // CPX abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m <= x);
                set_flags(N | Z, m = x - m);
                break;
            }

            case 0xE0: { // CPX
                uint8_t imm = read_pc_inc();
                flag_change(C, imm <= x);
                set_flags(N | Z, imm = x - imm);
                break;
            }

            case 0xC0: { // CPY
                uint8_t imm = read_pc_inc();
                flag_change(C, imm <= y);
                set_flags(N | Z, imm = y - imm);
                break;
            }

            case 0x55: { // EOR zpg, X
                uint8_t zpg = read_pc_inc() + x;
                m = bus.read(zpg & 0xFF);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x41: { // EOR (ind, X)
                uint8_t zpg = (read_pc_inc() + x) & 0xFF;
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x4D: { // EOR abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x5D: { // EOR abs, X
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + x);
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x59: { // EOR abs, Y
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr + y);
                if((addr + y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x45: { // EOR
                uint8_t zpg = read_pc_inc();
                set_flags(N | Z, a = a ^ bus.read(zpg));
                break;
            }

            case 0x49: { // EOR
                uint8_t imm = read_pc_inc();
                set_flags(N | Z, a = a ^ imm);
                break;
            }

            case 0x51: { // EOR
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0xD1: { // CMP
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xC5: { // CMP
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xCD: { // CMP
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xC9: { // CMP
                uint8_t imm = read_pc_inc();
                flag_change(C, imm <= a);
                set_flags(N | Z, imm = a - imm);
                break;
            }

            case 0xD5: { // CMP
                uint8_t zpg = read_pc_inc() + x;
                m = bus.read(zpg & 0xFF);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xE4: { // CPX
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m <= x);
                set_flags(N | Z, m = x - m);
                break;
            }

            case 0xC4: { // CPY
                uint8_t zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m <= y);
                set_flags(N | Z, m = y - m);
                break;
            }

            case 0x85: { // STA
                uint8_t zpg = read_pc_inc();
                bus.write(zpg, a);
                break;
            }

            case 0x40: { // RTI
                p = stack_pull();
                uint8_t pcl = stack_pull();
                uint8_t pch = stack_pull();
                pc = pcl + pch * 256 + 1;
                break;
            }

            case 0x60: { // RTS
                uint8_t pcl = stack_pull();
                uint8_t pch = stack_pull();
                pc = pcl + pch * 256 + 1;
                break;
            }

            case 0x95: { // STA
                uint8_t zpg = read_pc_inc();
                bus.write((zpg + x) & 0xFF, a);
                break;
            }

            case 0x94: { // STY
                uint8_t zpg = read_pc_inc();
                bus.write((zpg + x) & 0xFF, y);
                break;
            }

            case 0x8E: { // STX abs
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                bus.write(addr, x);
                break;
            }

            case 0x86: { // STX
                uint8_t zpg = read_pc_inc();
                bus.write(zpg, x);
                break;
            }

            case 0x84: { // STY
                uint8_t zpg = read_pc_inc();
                bus.write(zpg, y);
                break;
            }

            case 0x8C: { // STY
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                bus.write(addr, y);
                break;
            }

            case 0x20: { // JSR
                stack_push((pc + 1) >> 8);
                stack_push((pc + 1) & 0xFF);
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                pc = addr;
                break;
            }

#if SUPPORT_65C02
            // 65C02 instructions

            case 0x80: { // BRA imm, 65C02
                int32_t rel = (read_pc_inc() + 128) % 256 - 128;
                if((pc + rel) / 256 != pc / 256)
                    clk.add_cpu_cycles(1);
                pc += rel;
                break;
            }

            case 0x64: { // STZ zpg, 65C02
                uint8_t zpg = read_pc_inc();
                bus.write(zpg, 0);
                break;
            }

            case 0x9C: { // STZ abs, 65C02
                uint8_t low = read_pc_inc();
                uint8_t high = read_pc_inc();
                uint16_t addr = low + high * 256;
                bus.write(addr, 0x0);
                break;
            }

            case 0xDA: { // PHX, 65C02
                stack_push(x);
                break;
            }

            case 0xB2: { // LDA (zpg), 65C02
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0x92: { // STA (zpg), 65C02
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                bus.write(addr, a);
                break;
            }

            case 0x72: { // ADC (zpg), 65C02
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                uint8_t carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    uint8_t bcd = a / 16 * 10 + a % 16;
                    flag_change(C, ((uint16_t)bcd + (uint16_t)m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, ((uint16_t)a + (uint16_t)m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x3A: { // DEC, 65C02
                set_flags(N | Z, a = a - 1);
                break;
            }

            case 0x1A: { // INC, 65C02
                set_flags(N | Z, a = a + 1);
                break;
            }

            case 0x12: { // ORA (ind), 65C02
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0xD2: { // CMP (zpg), 65C02 instruction
                uint8_t zpg = read_pc_inc();
                uint8_t low = bus.read(zpg);
                uint8_t high = bus.read((zpg + 1) & 0xFF);
                uint16_t addr = low + high * 256;
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }
#endif // SUPPORT_65C02

            default:
                printf("unhandled instruction %02X at %04X\n", inst, pc - 1);
                fflush(stdout);
                exit(1);
        }
        assert(cycles[inst] > 0);
        clk.add_cpu_cycles(cycles[inst]);
    }
};

template<class CLK, class BUS>
const int32_t CPU6502<CLK, BUS>::cycles[256] =
{
    /* 0x0- */ 7, 6, -1, -1, -1, 3, 5, -1, 3, 2, 2, -1, -1, 4, 6, -1,
    /* 0x1- */ 2, 5, 5, -1, -1, 4, 6, -1, 2, 4, 2, -1, -1, 4, 7, -1,
    /* 0x2- */ 6, 6, -1, -1, 3, 3, 5, -1, 4, 2, 2, -1, 4, 4, 6, -1,
    /* 0x3- */ 2, 5, -1, -1, -1, 4, 6, -1, 2, 4, 2, -1, -1, 4, 7, -1,
    /* 0x4- */ 6, 6, -1, -1, -1, 3, 5, -1, 3, 2, 2, -1, 3, 4, 6, -1,
    /* 0x5- */ 2, 5, -1, -1, -1, 4, 6, -1, 2, 4, -1, -1, -1, 4, 7, -1,
    /* 0x6- */ 6, 6, -1, -1, 3, 3, 5, -1, 4, 2, 2, -1, 5, 4, 6, -1,
    /* 0x7- */ 2, 5, 5, -1, -1, 4, 6, -1, 2, 4, -1, -1, -1, 4, 7, -1,
    /* 0x8- */ 2, 6, -1, -1, 3, 3, 3, -1, 2, -1, 2, -1, 4, 4, 4, -1,
    /* 0x9- */ 2, 6, 5, -1, 4, 4, 4, -1, 2, 5, 2, -1, 4, 5, -1, -1,
    /* 0xA- */ 2, 6, 2, -1, 3, 3, 3, -1, 2, 2, 2, -1, 4, 4, 4, -1,
    /* 0xB- */ 2, 5, 5, -1, 4, 4, 4, -1, 2, 4, 2, -1, 4, 4, 4, -1,
    /* 0xC- */ 2, 6, -1, -1, 3, 3, 5, -1, 2, 2, 2, -1, 4, 4, 3, -1,
    /* 0xD- */ 2, 5, 5, -1, -1, 4, 6, -1, 2, 4, 3, -1, -1, 4, 7, -1,
    /* 0xE- */ 2, 6, -1, -1, 3, 3, 5, -1, 2, 2, 2, -1, 4, 4, 6, -1,
    /* 0xF- */ 2, 5, -1, -1, -1, 4, 6, -1, 2, 4, -1, -1, -1, 4, 7, -1,
};

#endif // CPU6502_H

