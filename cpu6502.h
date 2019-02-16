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
        unsigned char read(int addr);
        void read(int addr, unsigned char data);
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

    static int cycles[256];

    unsigned char a, x, y, s, p;
    static const unsigned char N = 0x80;
    static const unsigned char V = 0x40;
    static const unsigned char B = 0x10;
    static const unsigned char D = 0x08;
    static const unsigned char I = 0x04;
    static const unsigned char Z = 0x02;
    static const unsigned char C = 0x01;
    int pc = 0;

    enum Exception {
        NONE,
        RESET,
        NMI,
        BRK,
        INT,
    } exception;

    void stack_push(unsigned char d)
    {
        bus.write(0x100 + s--, d);
    }

    unsigned char stack_pull()
    {
        return bus.read(0x100 + ++s);
    }

    unsigned char read_pc_inc()
    {
        return bus.read(pc++);
    }

    void flag_change(unsigned char flag, bool v)
    {
        if(v)
            p |= flag;
        else
            p &= ~flag;
    }

    void flag_set(unsigned char flag)
    {
        p |= flag;
    }

    void flag_clear(unsigned char flag)
    {
        p &= ~flag;
    }

    int carry()
    {
        return (p & C) ? 1 : 0;
    }

    bool isset(unsigned char flag)
    {
        return (p & flag) != 0;
    }

    void set_flags(unsigned char flags, unsigned char v)
    {
        if(flags & Z)
            flag_change(Z, v == 0x00);
        if(flags & N)
            flag_change(N, v & 0x80);
    }

    static bool sbc_overflow_d(unsigned char a, unsigned char b, int borrow)
    {
        signed char a_ = a;
        signed char b_ = b;
        signed short c = a_ - (b_ + borrow);
        return (c < 0) || (c > 99);
    }

    static bool adc_overflow_d(unsigned char a, unsigned char b, int carry)
    {
        signed char a_ = a;
        signed char b_ = b;
        signed short c = a_ + b_ + carry;
        return (c < 0) || (c > 99);
    }

    static bool sbc_overflow(unsigned char a, unsigned char b, int borrow)
    {
        signed char a_ = a;
        signed char b_ = b;
        signed short c = a_ - (b_ + borrow);
        return (c < -128) || (c > 127);
    }

    static bool adc_overflow(unsigned char a, unsigned char b, int carry)
    {
        signed char a_ = a;
        signed char b_ = b;
        signed short c = a_ + b_ + carry;
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
        pc = bus.read(0xFFFC) + bus.read(0xFFFD) * 256;
        exception = NONE;
    }

    void irq()
    {
        stack_push((pc - 1) >> 8);
        stack_push((pc - 1) & 0xFF);
        stack_push(p);
        pc = bus.read(0xFFFE) + bus.read(0xFFFF) * 256;
        exception = NONE;
    }

    void nmi()
    {
        stack_push((pc - 1) >> 8);
        stack_push((pc - 1) & 0xFF);
        stack_push(p);
        pc = bus.read(0xFFFA) + bus.read(0xFFFB) * 256;
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

        unsigned char inst = read_pc_inc();

        unsigned char m;

        switch(inst) {
            case 0x00: { // BRK
                stack_push((pc - 1) >> 8);
                stack_push((pc - 1) & 0xFF);
                stack_push(p | B); // | B says the Synertek 6502 reference
                pc = bus.read(0xFFFE) + bus.read(0xFFFF) * 256;
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
                int zpg = read_pc_inc();
                set_flags(N | Z, m = bus.read(zpg) - 1);
                bus.write(zpg, m);
                break;
            }

            case 0xDE: { // DEC abs, X
                int addr = read_pc_inc() + read_pc_inc() * 256 + x;
                set_flags(N | Z, m = bus.read(addr) - 1);
                bus.write(addr, m);
                break;
            }

            case 0xCE: { // DEC abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                set_flags(N | Z, m = bus.read(addr) - 1);
                bus.write(addr, m);
                break;
            }

            case 0xCA: { // DEX
                set_flags(N | Z, x = x - 1);
                break;
            }

            case 0xFE: { // INC abs, X
                int addr = read_pc_inc() + read_pc_inc() * 256 + x;
                if((addr - x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, m = bus.read(addr) + 1);
                bus.write(addr, m);
                break;
            }

            case 0xEE: { // INC abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                set_flags(N | Z, m = bus.read(addr) + 1);
                bus.write(addr, m);
                break;
            }

            case 0xE6: { // INC zpg
                int zpg = read_pc_inc();
                set_flags(N | Z, m = bus.read(zpg) + 1);
                bus.write(zpg, m);
                break;
            }

            case 0xF6: { // INC zpg, X
                int zpg = (read_pc_inc() + x) & 0xFF;
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
                int rel = (read_pc_inc() + 128) % 256 - 128;
                if(!isset(N)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0x50: { // BVC
                int rel = (read_pc_inc() + 128) % 256 - 128;
                if(!isset(V)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0x70: { // BVS
                int rel = (read_pc_inc() + 128) % 256 - 128;
                if(isset(V)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0x30: { // BMI
                int rel = (read_pc_inc() + 128) % 256 - 128;
                if(isset(N)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0x90: { // BCC
                int rel = (read_pc_inc() + 128) % 256 - 128;
                if(!isset(C)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0xB0: { // BCS
                int rel = (read_pc_inc() + 128) % 256 - 128;
                if(isset(C)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0xD0: { // BNE
                int rel = (read_pc_inc() + 128) % 256 - 128;
                if(!isset(Z)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }

            case 0xF0: { // BEQ
                int rel = (read_pc_inc() + 128) % 256 - 128;
                if(isset(Z)) {
                    clk.add_cpu_cycles(1);
                    if((pc + rel) / 256 != pc / 256)
                        clk.add_cpu_cycles(1);
                    pc += rel;
                }
                break;
            }


            case 0xA1: { // LDA (ind, X)
                unsigned char zpg = (read_pc_inc() + x) & 0xFF;
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xB5: { // LDA zpg, X
                unsigned char zpg = read_pc_inc();
                int addr = zpg + x;
                set_flags(N | Z, a = bus.read(addr & 0xFF));
                break;
            }

            case 0xB1: { // LDA ind, Y
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xA5: { // LDA zpg
                unsigned char zpg = read_pc_inc();
                set_flags(N | Z, a = bus.read(zpg));
                break;
            }

            case 0xDD: { // CMP abs, X
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr + x);
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xC1: { // CMP (ind, X)
                unsigned char zpg = (read_pc_inc() + x) & 0xFF;
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xD9: { // CMP abs, Y
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr + y);
                if((addr + y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xB9: { // LDA abs, Y
                int addr = read_pc_inc() + read_pc_inc() * 256;
                set_flags(N | Z, a = bus.read(addr + y));
                if((addr + y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                break;
            }

            case 0xBC: { // LDY abs, X
                int addr = read_pc_inc() + read_pc_inc() * 256;
                set_flags(N | Z, y = bus.read(addr + x));
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                break;
            }

            case 0xBD: { // LDA abs, X
                int addr = read_pc_inc() + read_pc_inc() * 256;
                set_flags(N | Z, a = bus.read(addr + x));
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                break;
            }

            case 0xF5: { // SBC zpg, X
                unsigned char zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                int borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
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
                unsigned char zpg = read_pc_inc();
                m = bus.read(zpg);
                int borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
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
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xff) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                int borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
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
                int addr = read_pc_inc() + read_pc_inc() * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                unsigned char m = bus.read(addr);
                int borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
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
                int addr = read_pc_inc() + read_pc_inc() * 256 + x;
                if((addr - x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                unsigned char m = bus.read(addr);
                int borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
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
                int addr = read_pc_inc() + read_pc_inc() * 256;
                unsigned char m = bus.read(addr);
                int borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
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
                unsigned char m = read_pc_inc();
                int borrow = isset(C) ? 0 : 1;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
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
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                int carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
                    flag_change(C, (int)(bcd + m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, (int)(a + m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x6D: { // ADC abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                int carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
                    flag_change(C, (int)(bcd + m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, (int)(a + m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x65: { // ADC
                unsigned char zpg = read_pc_inc();
                m = bus.read(zpg);
                int carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
                    flag_change(C, (int)(bcd + m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, (int)(a + m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x7D: { // ADC abs, X
                int addr = read_pc_inc() + read_pc_inc() * 256 + x;
                if((addr - x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                int carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
                    flag_change(C, (int)(bcd + m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, (int)(a + m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x79: { // ADC abs, Y
                int addr = read_pc_inc() + read_pc_inc() * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                int carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
                    flag_change(C, (int)(bcd + m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, (int)(a + m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x69: { // ADC
                m = read_pc_inc();
                int carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
                    flag_change(C, (int)(bcd + m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, (int)(a + m + carry) > 0xFF);
                    flag_change(V, adc_overflow(a, m, carry));
                    set_flags(N | Z, a = a + m + carry);
                }
                break;
            }

            case 0x0E: { // ASL abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = m << 1);
                bus.write(addr, m);
                break;
            }

            case 0x06: { // ASL
                unsigned char zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = m << 1);
                bus.write(zpg, m);
                break;
            }

            case 0x16: { // ASL
                unsigned char zpg = read_pc_inc();
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
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr + x);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                bus.write(addr + x, m);
                break;
            }

            case 0x46: { // LSR
                unsigned char zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                bus.write(zpg, m);
                break;
            }

            case 0x56: { // LSR zpg, X
                unsigned char zpg = read_pc_inc() + x;
                m = bus.read(zpg & 0xFF);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                bus.write(zpg, m);
                break;
            }

            case 0x4E: { // LSR
                int addr = read_pc_inc() + read_pc_inc() * 256;
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
                unsigned char zpg = (read_pc_inc() + x) & 0xFF;
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x15: { // ORA zpg, X
                int zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x0D: { // ORA abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x19: { // ORA abs, Y
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr + y);
                if((addr + y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x1D: { // ORA abs, X
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr + x);
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x11: { // ORA (ind), Y
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x05: { // ORA zpg
                unsigned char zpg = read_pc_inc();
                m = bus.read(zpg);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x09: { // ORA imm
                unsigned char imm = read_pc_inc();
                set_flags(N | Z, a = a | imm);
                break;
            }

            case 0x35: { // AND zpg, X
                int zpg = (read_pc_inc() + x) & 0xFF;
                set_flags(N | Z, a = a & bus.read(zpg));
                break;
            }

            case 0x31: { // AND (ind), y
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = a & bus.read(addr));
                break;
            }

            case 0x3D: { // AND abs, x
                int addr = read_pc_inc() + read_pc_inc() * 256;
                set_flags(N | Z, a = a & bus.read(addr + x));
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                break;
            }

            case 0x39: { // AND abs, y
                int addr = read_pc_inc() + read_pc_inc() * 256;
                set_flags(N | Z, a = a & bus.read(addr + y));
                if((addr + y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                break;
            }

            case 0x2D: { // AND abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                set_flags(N | Z, a = a & bus.read(addr));
                break;
            }

            case 0x25: { // AND zpg
                unsigned char zpg = read_pc_inc();
                set_flags(N | Z, a = a & bus.read(zpg));
                break;
            }

            case 0x29: { // AND imm
                unsigned char imm = read_pc_inc();
                set_flags(N | Z, a = a & imm);
                break;
            }

            case 0x88: { // DEY
                set_flags(N | Z, y = y - 1);
                break;
            }

            case 0x7E: { // ROR abs, X
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr + x);
                bool c = isset(C);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                bus.write(addr + x, m);
                break;
            }

            case 0x36: { // ROL zpg,X
                unsigned char zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                bus.write(zpg, m);
                break;
            }


            case 0x3E: { // ROL abs, X
                int addr = read_pc_inc() + read_pc_inc() * 256;
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
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                bus.write(addr, m);
                break;
            }

            case 0x66: { // ROR
                unsigned char zpg = read_pc_inc();
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                bus.write(zpg, m);
                break;
            }

            case 0x76: { // ROR
                unsigned char zpg = (read_pc_inc() + x) & 0xFF;
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                bus.write(zpg, m);
                break;
            }

            case 0x2E: { // ROL abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                bool c = isset(C);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                bus.write(addr, m);
                break;
            }


            case 0x26: { // ROL
                unsigned char zpg = read_pc_inc();
                bool c = isset(C);
                m = bus.read(zpg);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                bus.write(zpg, m);
                break;
            }

            case 0x4C: { // JMP
                int addr = read_pc_inc() + read_pc_inc() * 256;
                pc = addr;
                break;
            }

            case 0x6C: { // JMP
                int addr = read_pc_inc() + read_pc_inc() * 256;
                unsigned char addrl = bus.read(addr);
                unsigned char addrh = bus.read(addr + 1);
                addr = addrl + addrh * 256;
                pc = addr;
                break;
            }

            case 0x9D: { // STA abs, x
                int addr = read_pc_inc() + read_pc_inc() * 256;
                bus.write(addr + x, a);
                break;
            }

            case 0x99: { // STA
                int addr = read_pc_inc() + read_pc_inc() * 256;
                bus.write(addr + y, a);
                break;
            }

            case 0x91: { // STA (ind), Y
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                bus.write(addr, a);
                break;
            }

            case 0x81: { // STA (ind, X)
                unsigned char zpg = (read_pc_inc() + x) & 0xFF;
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
                bus.write(addr, a);
                break;
            }

            case 0x8D: { // STA
                int addr = read_pc_inc() + read_pc_inc() * 256;
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
                unsigned char zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(Z, (a & m) == 0);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x40);
                break;
            }

            case 0x2C: { // BIT
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                flag_change(Z, (a & m) == 0);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x40);
                break;
            }

            case 0xB4: { // LDY
                unsigned char zpg = read_pc_inc();
                set_flags(N | Z, y = bus.read((zpg + x) & 0xFF));
                break;
            }

            case 0xAE: { // LDX abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                set_flags(N | Z, x = bus.read(addr));
                break;
            }

            case 0xBE: { // LDX
                int addr = read_pc_inc() + read_pc_inc() * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, x = bus.read(addr));
                break;
            }

            case 0xA6: { // LDX
                unsigned char zpg = read_pc_inc();
                set_flags(N | Z, x = bus.read(zpg));
                break;
            }

            case 0xB6: { // LDX zpg, Y
                int zpg = (read_pc_inc() + y) & 0xFF;
                set_flags(N | Z, x = bus.read(zpg));
                break;
            }

            case 0xA4: { // LDY
                unsigned char zpg = read_pc_inc();
                set_flags(N | Z, y = bus.read(zpg));
                break;
            }

            case 0xAC: { // LDY
                int addr = read_pc_inc() + read_pc_inc() * 256;
                set_flags(N | Z, y = bus.read(addr));
                break;
            }

            case 0xA2: { // LDX
                unsigned char imm = read_pc_inc();
                set_flags(N | Z, x = imm);
                break;
            }

            case 0xA0: { // LDY
                unsigned char imm = read_pc_inc();
                set_flags(N | Z, y = imm);
                break;
            }

            case 0xA9: { // LDA
                unsigned char imm = read_pc_inc();
                set_flags(N | Z, a = imm);
                break;
            }

            case 0xAD: { // LDA
                int addr = read_pc_inc() + read_pc_inc() * 256;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xCC: { // CPY abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                flag_change(C, m <= y);
                set_flags(N | Z, m = y - m);
                break;
            }

            case 0xEC: { // CPX abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                flag_change(C, m <= x);
                set_flags(N | Z, m = x - m);
                break;
            }

            case 0xE0: { // CPX
                unsigned char imm = read_pc_inc();
                flag_change(C, imm <= x);
                set_flags(N | Z, imm = x - imm);
                break;
            }

            case 0xC0: { // CPY
                unsigned char imm = read_pc_inc();
                flag_change(C, imm <= y);
                set_flags(N | Z, imm = y - imm);
                break;
            }

            case 0x55: { // EOR zpg, X
                unsigned char zpg = read_pc_inc() + x;
                m = bus.read(zpg & 0xFF);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x41: { // EOR (ind, X)
                unsigned char zpg = (read_pc_inc() + x) & 0xFF;
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x4D: { // EOR abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x5D: { // EOR abs, X
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr + x);
                if((addr + x) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x59: { // EOR abs, Y
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr + y);
                if((addr + y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x45: { // EOR
                unsigned char zpg = read_pc_inc();
                set_flags(N | Z, a = a ^ bus.read(zpg));
                break;
            }

            case 0x49: { // EOR
                unsigned char imm = read_pc_inc();
                set_flags(N | Z, a = a ^ imm);
                break;
            }

            case 0x51: { // EOR
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0xD1: { // CMP
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk.add_cpu_cycles(1);
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xC5: { // CMP
                unsigned char zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xCD: { // CMP
                int addr = read_pc_inc() + read_pc_inc() * 256;
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xC9: { // CMP
                unsigned char imm = read_pc_inc();
                flag_change(C, imm <= a);
                set_flags(N | Z, imm = a - imm);
                break;
            }

            case 0xD5: { // CMP
                unsigned char zpg = read_pc_inc() + x;
                m = bus.read(zpg & 0xFF);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xE4: { // CPX
                unsigned char zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m <= x);
                set_flags(N | Z, m = x - m);
                break;
            }

            case 0xC4: { // CPY
                unsigned char zpg = read_pc_inc();
                m = bus.read(zpg);
                flag_change(C, m <= y);
                set_flags(N | Z, m = y - m);
                break;
            }

            case 0x85: { // STA
                unsigned char zpg = read_pc_inc();
                bus.write(zpg, a);
                break;
            }

            case 0x40: { // RTI
                p = stack_pull();
                unsigned char pcl = stack_pull();
                unsigned char pch = stack_pull();
                pc = pcl + pch * 256 + 1;
                break;
            }

            case 0x60: { // RTS
                unsigned char pcl = stack_pull();
                unsigned char pch = stack_pull();
                pc = pcl + pch * 256 + 1;
                break;
            }

            case 0x95: { // STA
                unsigned char zpg = read_pc_inc();
                bus.write((zpg + x) & 0xFF, a);
                break;
            }

            case 0x94: { // STY
                unsigned char zpg = read_pc_inc();
                bus.write((zpg + x) & 0xFF, y);
                break;
            }

            case 0x8E: { // STX abs
                int addr = read_pc_inc() + read_pc_inc() * 256;
                bus.write(addr, x);
                break;
            }

            case 0x86: { // STX
                unsigned char zpg = read_pc_inc();
                bus.write(zpg, x);
                break;
            }

            case 0x84: { // STY
                unsigned char zpg = read_pc_inc();
                bus.write(zpg, y);
                break;
            }

            case 0x8C: { // STY
                int addr = read_pc_inc() + read_pc_inc() * 256;
                bus.write(addr, y);
                break;
            }

            case 0x20: { // JSR
                stack_push((pc + 1) >> 8);
                stack_push((pc + 1) & 0xFF);
                int addr = read_pc_inc() + read_pc_inc() * 256;
                pc = addr;
                break;
            }

#if SUPPORT_65C02
            // 65C02 instructions

            case 0x80: { // BRA imm, 65C02
                int rel = (read_pc_inc() + 128) % 256 - 128;
                if((pc + rel) / 256 != pc / 256)
                    clk.add_cpu_cycles(1);
                pc += rel;
                break;
            }

            case 0x64: { // STZ zpg, 65C02
                unsigned char zpg = read_pc_inc();
                bus.write(zpg, 0);
                break;
            }

            case 0x9C: { // STZ abs, 65C02
                int addr = read_pc_inc() + read_pc_inc() * 256;
                bus.write(addr, 0x0);
                break;
            }

            case 0xDA: { // PHX, 65C02
                stack_push(x);
                break;
            }

            case 0xB2: { // LDA (zpg), 65C02
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0x92: { // STA (zpg), 65C02
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
                bus.write(addr, a);
                break;
            }

            case 0x72: { // ADC (zpg), 65C02
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;

                m = bus.read(addr);
                int carry = isset(C) ? 1 : 0;
                if(isset(D)) {
                    unsigned char bcd = a / 16 * 10 + a % 16;
                    flag_change(C, (int)(bcd + m + carry) > 99);
                    flag_change(V, adc_overflow_d(bcd, m, carry));
                    set_flags(N | Z, bcd = bcd + m + carry);
                    a = bcd / 10 * 16 + bcd % 10;
                } else {
                    flag_change(C, (int)(a + m + carry) > 0xFF);
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
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0xD2: { // CMP (zpg), 65C02 instruction
                unsigned char zpg = read_pc_inc();
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
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
int CPU6502<CLK, BUS>::cycles[256] =
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

