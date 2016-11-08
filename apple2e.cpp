#include <cstdlib>
#include <string>
#include <set>
#include <chrono>
#include <thread>
#include <ratio>
#include <iostream>
#include <signal.h>

using namespace std;

#include "emulator.h"
#include "keyboard.h"
#include "dis6502.h"

const unsigned int DEBUG_ERROR = 0x01;
const unsigned int DEBUG_DECODE = 0x02;
const unsigned int DEBUG_STATE = 0x04;
const unsigned int DEBUG_RW = 0x08;
const unsigned int DEBUG_BUS = 0x10;
volatile unsigned int debug = DEBUG_ERROR | DEBUG_DECODE | DEBUG_STATE; // | DEBUG_RW;

struct SoftSwitch
{
    string name;
    int clear_address;
    int set_address;
    int read_address;
    bool read_also_changes;
    bool sw = false;
    bool implemented;
    SoftSwitch(const char* name_, int clear, int on, int read, bool read_changes, vector<SoftSwitch*>& s, bool implemented_ = false) :
        name(name_),
        clear_address(clear),
        set_address(on),
        read_address(read),
        read_also_changes(read_changes),
        implemented(implemented_)
    {
        s.push_back(this);
    }
    operator bool() const
    {
        return sw;
    }
};

const int textport_row_base_addresses[] = 
{
    0x400,
    0x480,
    0x500,
    0x580,
    0x600,
    0x680,
    0x700,
    0x780,
    0x428,
    0x4A8,
    0x528,
    0x5A8,
    0x628,
    0x6A8,
    0x728,
    0x7A8,
    0x450,
    0x4D0,
    0x550,
    0x5D0,
    0x650,
    0x6D0,
    0x750,
    0x7D0,
};

void textport_change(int page, unsigned char *textport)
{
    printf("TEXTPORT:\n");
    printf("------------------------------------------\n");
    for(int row = 0; row < 24; row++) {
        printf("|");
        for(int col = 0; col < 40; col++) {
            int addr = textport_row_base_addresses[row] + col + ((page == 1) ? 0x0 : 0x400);
            int ch = textport[addr] & 0x7F;
            printf("%c", isprint(ch) ? ch : '?');
        }
        printf("|\n");
    }
    printf("------------------------------------------\n");
}

struct region {
    const int base;
    const int size;
    bool contains(int addr) const
    {
        return addr >= base && addr < base + size;
    }
};

const region hires1_region = {0x2000, 0x2000};
const region hires2_region = {0x4000, 0x2000};
const region text1_region = {0x400, 0x400};
const region text2_region = {0x800, 0x400};
const region io_region = {0xC000, 0x100};
const region irom_region = {0xC100, 0x0F00};
const region rom_region = {0xD000, 0x3000};

struct MAINboard : board_base
{
    unsigned char rom_bytes[0x3000/*rom_region.size*/];
    unsigned char irom_bytes[0x0F00/*irom_region.size*/];
    unsigned char ram_bytes[65536];

    vector<SoftSwitch*> switches;
    SoftSwitch CXROM {"CXROM", 0xC006, 0xC007, 0xC015, false, switches, true};
    SoftSwitch STORE80 {"STORE80", 0xC000, 0xC001, 0xC018, false, switches};
    SoftSwitch RAMRD {"RAMRD", 0xC002, 0xC003, 0xC013, false, switches};
    SoftSwitch RAMWRT {"RAMWRT", 0xC004, 0xC005, 0xC014, false, switches};
    SoftSwitch ALTZP {"ALTZP", 0xC008, 0xC009, 0xC016, false, switches};
    SoftSwitch C3ROM {"C3ROM", 0xC00A, 0xC00B, 0xC017, false, switches};
    SoftSwitch ALTCHAR {"ALTCHAR", 0xC00E, 0xC00F, 0xC01E, false, switches};
    SoftSwitch VID80 {"VID80", 0xC00C, 0xC00D, 0xC01F, false, switches};
    SoftSwitch TEXT {"TEXT", 0xC050, 0xC051, 0xC01A, true, switches, true};
    SoftSwitch MIXED {"MIXED", 0xC052, 0xC053, 0xC01B, true, switches};
    SoftSwitch PAGE2 {"PAGE2", 0xC054, 0xC055, 0xC01C, true, switches};
    SoftSwitch HIRES {"HIRES", 0xC056, 0xC057, 0xC01D, true, switches};

    set<int> ignore_mmio = {0xC058, 0xC05A, 0xC05D, 0xC05F, 0xC061, 0xC062};

    MAINboard(unsigned char rom[32768])
    {
        memcpy(rom_bytes, rom + rom_region.base - 0x8000 , sizeof(rom_bytes));
        memcpy(irom_bytes, rom + irom_region.base - 0x8000 , sizeof(irom_bytes));
        memset(ram_bytes, 0x00, sizeof(ram_bytes));
        start_keyboard();
    }

    virtual ~MAINboard()
    {
        stop_keyboard();
    }
    virtual bool read(int addr, unsigned char &data)
    {
        if(debug & DEBUG_RW) printf("MAIN board read\n");
        if(io_region.contains(addr)) {
            for(auto it = switches.begin(); it != switches.end(); it++) {
                SoftSwitch* sw = *it;
                if(addr == sw->read_address) {
                    data = sw->sw ? 0x80 : 0x00;
                    if(debug & DEBUG_RW) printf("Read status of %s = %02X\n", sw->name.c_str(), data);
                    return true;
                } else if(sw->read_also_changes && addr == sw->set_address) {
                    if(!sw->implemented) { printf("%s ; set is unimplemented\n", sw->name.c_str()); exit(0); }
                    data = 0xff;
                    sw->sw = true;
                    if(debug & DEBUG_RW) printf("Set %s\n", sw->name.c_str());
                    return true;
                } else if(sw->read_also_changes && addr == sw->clear_address) {
                    // if(!sw->implemented) { printf("%s ; unimplemented\n", sw->name.c_str()); exit(0); }
                    data = 0xff;
                    sw->sw = false;
                    if(debug & DEBUG_RW) printf("Clear %s\n", sw->name.c_str());
                    return true;
                }
            }
            if(ignore_mmio.find(addr) != ignore_mmio.end()) {
                if(debug & DEBUG_RW) printf("read %04X, ignored, return 0x00\n", addr);
                data = 0x00;
                return true;
            }
            if(addr == 0xC000) {
                data = get_keyboard_data_and_strobe();
                if(debug & DEBUG_RW) printf("read KBD, return 0x%02X\n", data);
                return true;
            }
            if(addr == 0xC030) {
                if(debug & DEBUG_RW) printf("read SPKR, force 0x00\n");
                // click
                data = 0x00;
                return true;
            }
            if(addr == 0xC010) {
                // reset keyboard latch
                data = get_any_key_down_and_clear_strobe();
                if(debug & DEBUG_RW) printf("read KBDSTRB, return 0x%02X\n", data);
                return true;
            }
            printf("unhandled MMIO Read at %04X\n", addr);
            exit(0);
        }
        if(CXROM && irom_region.contains(addr)) {
            data = irom_bytes[addr - irom_region.base];
            if(debug & DEBUG_RW) printf("read 0x%04X -> 0x%02X from internal ROM\n", addr, data);
            return true;
        }
        if(rom_region.contains(addr)) {
            data = rom_bytes[addr - rom_region.base];
            if(debug & DEBUG_RW) printf("read 0x%04X -> 0x%02X from ROM\n", addr, data);
            return true;
        }
        if(addr >= 0 && addr < sizeof(ram_bytes)) {
            data = ram_bytes[addr];
            if(debug & DEBUG_RW) printf("read 0x%04X -> 0x%02X from RAM\n", addr, data);
            return true;
        }
        return false;
    }
    virtual bool write(int addr, unsigned char data)
    {
        if(TEXT) {
            // TEXT takes precedence over all other modes
            if(text1_region.contains(addr)) {
                printf("TEXT1 WRITE!\n");
                if(!PAGE2) textport_change(1, ram_bytes);
            }
            if(text2_region.contains(addr)) {
                printf("TEXT2 WRITE!\n");
                if(PAGE2) textport_change(2, ram_bytes);
            }
        } else {
            // MIXED shows text in last 4 columns in both HIRES or LORES
            if(MIXED) {
                printf("MIXED WRITE, abort!\n");
                exit(0);
            } else {
                if(HIRES) {
                    if(hires1_region.contains(addr)) {
                        printf("HIRES1 WRITE, abort!\n");
                        exit(0);
                    }
                    if(hires2_region.contains(addr)) {
                        printf("HIRES2 WRITE, abort!\n");
                        exit(0);
                    }
                } else {
                    if(text1_region.contains(addr)) {
                        printf("LORES1 WRITE, abort!\n");
                        exit(0);
                    }
                    if(text2_region.contains(addr)) {
                        printf("LORES2 WRITE, abort!\n");
                        exit(0);
                    }
                }
            }
        }
        if(io_region.contains(addr)) {
            for(auto it = switches.begin(); it != switches.end(); it++) {
                SoftSwitch* sw = *it;
                if(addr == sw->set_address) {
                    if(!sw->implemented) { printf("%s ; set is unimplemented\n", sw->name.c_str()); exit(0); }
                    data = 0xff;
                    sw->sw = true;
                    if(debug & DEBUG_RW) printf("Set %s\n", sw->name.c_str());
                    return true;
                } else if(addr == sw->clear_address) {
                    // if(!sw->implemented) { printf("%s ; unimplemented\n", sw->name.c_str()); exit(0); }
                    data = 0xff;
                    sw->sw = false;
                    if(debug & DEBUG_RW) printf("Clear %s\n", sw->name.c_str());
                    return true;
                }
            }
            if(addr == 0xC010) {
                if(debug & DEBUG_RW) printf("write KBDSTRB\n");
                // reset keyboard latch
                get_any_key_down_and_clear_strobe();
                return true;
            }
            if(addr == 0xC030) {
                if(debug & DEBUG_RW) printf("write SPKR\n");
                // click
                return true;
            }
            printf("unhandled MMIO Write at %04X\n", addr);
            exit(0);
        }
        if(rom_region.contains(addr)) {
            return false;
        }
        if(CXROM && irom_region.contains(addr)) {
            return false;
        }
        if(addr >= 0 && addr < sizeof(ram_bytes)) {
            ram_bytes[addr] = data;
            if(debug & DEBUG_RW) printf("wrote 0x%02X to RAM 0x%04X\n", data, addr);
            return true;
        }
        return false;
    }
};

struct bus_controller
{
    std::vector<board_base*> boards;
    unsigned char read(int addr)
    {
        for(auto b = boards.begin(); b != boards.end(); b++) {
            unsigned char data = 0xaa;
            if((*b)->read(addr, data)) {
                if(debug & DEBUG_BUS) printf("read %04X returned %02X\n", addr, data);
                return data;
            }
        }
        if(debug & DEBUG_ERROR)
            fprintf(stderr, "no ownership of read at %04X\n", addr);
        return 0xAA;
    }
    void write(int addr, unsigned char data)
    {
        for(auto b = boards.begin(); b != boards.end(); b++) {
            if((*b)->write(addr, data)) {
                if(debug & DEBUG_BUS) printf("write %04X %02X\n", addr, data);
                return;
            }
        }
        if(debug & DEBUG_ERROR)
            fprintf(stderr, "no ownership of write %02X at %04X\n", data, addr);
    }
};

bus_controller bus;

struct CPU6502
{
    unsigned char a, x, y, s, p;
    static const unsigned char N = 0x80;
    static const unsigned char V = 0x40;
    static const unsigned char B = 0x10;
    static const unsigned char D = 0x08;
    static const unsigned char I = 0x04;
    static const unsigned char Z = 0x02;
    static const unsigned char C = 0x01;
    int pc;
    enum Exception {
        NONE,
        RESET,
        NMI,
        BRK,
        INT,
    } exception;
    CPU6502() :
        exception(RESET)
    {
    }
    void stack_push(bus_controller& bus, unsigned char d)
    {
        bus.write(0x100 + s--, d);
    }
    unsigned char stack_pull(bus_controller& bus)
    {
        return bus.read(0x100 + ++s);
    }
    unsigned char read_pc_inc(bus_controller& bus)
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
    void reset(bus_controller& bus)
    {
        s = 0xFD;
        pc = bus.read(0xFFFC) + bus.read(0xFFFD) * 256;
        exception = NONE;
    }
    enum Operand {
        A,
        IMPL,
        REL,
        ABS,
        ABS_X,
        ABS_Y,
        IND,
        X_IND,
        IND_Y,
        ZPG,
        ZPG_X,
        ZPG_Y,
        IMM,
        UND,
    };
    int carry()
    {
        return (p & C) ? 1 : 0;
    }
    bool isset(unsigned char flag)
    {
        return (p & flag) != 0;
    }
#if 0
    int get_operand(bus_controller& bus, Operand oper)
    {
        switch(oper)
        {
            case A: return 0;
            case UND: return 0;
            case IMPL: return 0;
            case REL: return (bus.read(pc) + 128) % 256 - 128;
            case ABS: return bus.read(pc) + bus.read(pc + 1) * 256;
            case ABS_Y: return bus.read(pc) + bus.read(pc + 1) * 256 + y + carry;
            case ABS_X: return bus.read(pc) + bus.read(pc + 1) * 256 + x + carry;
            case ZPG: return bus.read(pc);
            case ZPG_Y: return (bus.read(pc) + y) & 0xFF;
            case ZPG_X: return (bus.read(pc) + x) & 0xFF;
            case IND: return bus.read(bus.read(pc) + bus.read(pc + 1) * 256);
        }
    }
#endif
    void set_flags(unsigned char flags, unsigned char v)
    {
        if(flags & Z)
            flag_change(Z, v == 0x00);
        if(flags & N)
            flag_change(N, v & 0x80);
    }
    void cycle(bus_controller& bus)
    {
        if(exception == RESET) {
            if(debug & DEBUG_STATE) printf("RESET\n");
            reset(bus);
        }

        unsigned char inst = read_pc_inc(bus);

        unsigned char m;

        int bytes;
        string dis;
        unsigned char buf[4];
        buf[0] = inst;
        buf[1] = bus.read(pc + 0);
        buf[2] = bus.read(pc + 1);
        buf[3] = bus.read(pc + 2);
        tie(bytes, dis) = disassemble_6502(pc - 1, buf);
        if(debug & DEBUG_DECODE) printf("%s\n", dis.c_str());
        switch(inst) {
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
                set_flags(N | Z, s = x);
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

            case 0xC6: { // DEC
                int zpg = read_pc_inc(bus);
                set_flags(N | Z, m = bus.read(zpg) - 1);
                bus.write(zpg, m);
                break;
            }

            case 0xCE: { // DEC
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, m = bus.read(addr) - 1);
                bus.write(addr, m);
                break;
            }

            case 0xCA: { // DEC
                set_flags(N | Z, x = x - 1);
                break;
            }

            case 0xE6: { // INC
                int zpg = read_pc_inc(bus);
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
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(!isset(N))
                    pc += rel;
                break;
            }

            case 0x50: { // BVC
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(!isset(V))
                    pc += rel;
                break;
            }

            case 0x70: { // BVS
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(isset(V))
                    pc += rel;
                break;
            }

            case 0x30: { // BMI
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(isset(N))
                    pc += rel;
                break;
            }

            case 0xB5: { // LDA
                unsigned char zpg = read_pc_inc(bus);
                int addr = zpg + y;
                set_flags(N | Z, a = bus.read(addr & 0xFF));
                break;
            }

            case 0xB1: { // LDA
                unsigned char zpg = read_pc_inc(bus);
                int addr = bus.read(zpg) + bus.read(zpg + 1) * 256 + y;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xA5: { // LDA
                unsigned char zpg = read_pc_inc(bus);
                set_flags(N | Z, a = bus.read(zpg));
                break;
            }

            case 0xDD: { // CMP
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr + x);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xD9: { // CMP
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr + y);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xB9: { // LDA
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, a = bus.read(addr + y));
                break;
            }

            case 0xBD: { // LDA
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, a = bus.read(addr + x));
                break;
            }

            case 0x65: { // ADC
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                int carry = isset(C) ? 1 : 0;
                flag_change(C, (int)(a + m + carry) > 0xFF);
                a = a + m + carry;
                flag_change(N, a & 0x80);
                flag_change(V, isset(C) != isset(N));
                flag_change(Z, a == 0);
                break;
            }

            case 0xF1: { // SBC
                unsigned char zpg = read_pc_inc(bus);
                int addr = bus.read(zpg) + bus.read(zpg + 1) * 256 + y;
                m = bus.read(addr);
                int borrow = isset(C) ? 0 : 1;
                flag_change(C, !(a < m - borrow));
                a = a - m - borrow;
                flag_change(N, a & 0x80);
                flag_change(V, isset(C) != isset(N));
                flag_change(Z, a == 0);
                break;
            }
            case 0xED: { // SBC
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                unsigned char imm = bus.read(addr);
                int borrow = isset(C) ? 0 : 1;
                flag_change(C, !(a < imm - borrow));
                a = a - imm - borrow;
                flag_change(N, a & 0x80);
                flag_change(V, isset(C) != isset(N));
                flag_change(Z, a == 0);
                break;
            }

            case 0xE9: { // SBC
                unsigned char imm = read_pc_inc(bus);
                int borrow = isset(C) ? 0 : 1;
                flag_change(C, !(a < imm - borrow));
                a = a - imm - borrow;
                flag_change(N, a & 0x80);
                flag_change(V, isset(C) != isset(N));
                flag_change(Z, a == 0);
                break;
            }

            case 0x69: { // ADC
                unsigned char imm = read_pc_inc(bus);
                int carry = isset(C) ? 1 : 0;
                flag_change(C, (int)(a + imm + carry) > 0xFF);
                a = a + imm + carry;
                flag_change(N, a & 0x80);
                flag_change(V, isset(C) != isset(N));
                flag_change(Z, a == 0);
                break;
            }

            case 0x06: { // ASL
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = m << 1);
                bus.write(zpg, m);
                break;
            }

            case 0x0A: { // ASL
                flag_change(C, a & 0x80);
                set_flags(N | Z, a = a << 1);
                break;
            }

            case 0x46: { // LSR
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                bus.write(zpg, m);
                break;
            }

            case 0x4E: { // LSR
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
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
                set_flags(N | Z, a = stack_pull(bus));
                break;
            }

            case 0x48: { // PHA
                stack_push(bus, a);
                break;
            }

            case 0x05: { // ORA
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x09: { // ORA
                unsigned char imm = read_pc_inc(bus);
                set_flags(N | Z, a = a | imm);
                break;
            }

            case 0x25: { // AND
                unsigned char zpg = read_pc_inc(bus);
                set_flags(N | Z, a = a & bus.read(zpg));
                break;
            }

            case 0x29: { // AND
                unsigned char imm = read_pc_inc(bus);
                set_flags(N | Z, a = a & imm);
                break;
            }

            case 0x88: { // DEY
                set_flags(N | Z, y = y - 1);
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

            case 0x76: { // ROR
                unsigned char zpg = read_pc_inc(bus) + x;
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                bus.write(zpg, m);
                break;
            }

            case 0x26: { // ROL
                unsigned char zpg = read_pc_inc(bus) + x;
                bool c = isset(C);
                m = bus.read(zpg);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = (c ? 0x01 : 0x00) | (m << 1));
                bus.write(zpg, m);
                break;
            }

            case 0x4C: {
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                // JMP
                pc = addr;
                break;
            }

            case 0x6C: { // JMP
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                unsigned char addrl = bus.read(addr);
                unsigned char addrh = bus.read(addr + 1);
                addr = addrl + addrh * 256;
                pc = addr;
                break;
            }

            case 0x9D: { // STA
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                bus.write(addr + x, a);
                break;
            }

            case 0x99: { // STA
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                bus.write(addr + y, a);
                break;
            }

            case 0x91: { // STA
                unsigned char zpg = read_pc_inc(bus);
                int addr = bus.read(zpg) + bus.read(zpg + 1) * 256 + y;
                bus.write(addr, a);
                break;
            }

            case 0x8D: { // STA
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                bus.write(addr, a);
                break;
            }

            case 0x08: { // PHP
                stack_push(bus, p);
                break;
            }

            case 0x28: { // PLP
                p = stack_pull(bus);
                break;
            }

            case 0x24: { // BIT
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                flag_change(Z, a & m);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x70);
                break;
            }

            case 0x2C: { // BIT
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr);
                flag_change(Z, a & m);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x70);
                break;
            }

            case 0xB4: { // LDY
                unsigned char zpg = read_pc_inc(bus);
                set_flags(N | Z, y = bus.read(zpg + x));
                break;
            }

            case 0xA6: { // LDX
                unsigned char zpg = read_pc_inc(bus);
                set_flags(N | Z, x = bus.read(zpg));
                break;
            }

            case 0xA4: { // LDY
                unsigned char zpg = read_pc_inc(bus);
                set_flags(N | Z, y = bus.read(zpg));
                break;
            }

            case 0xAC: { // LDY
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, y = bus.read(addr));
                break;
            }

            case 0xA2: { // LDX
                unsigned char imm = read_pc_inc(bus);
                set_flags(N | Z, x = imm);
                break;
            }

            case 0xA0: { // LDY
                unsigned char imm = read_pc_inc(bus);
                set_flags(N | Z, y = imm);
                break;
            }

            case 0xA9: { // LDA
                unsigned char imm = read_pc_inc(bus);
                set_flags(N | Z, a = imm);
                break;
            }

            case 0xAD: {
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                // LDA
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xCC: { // CPY
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xE0: { // CPX
                unsigned char imm = read_pc_inc(bus);
                flag_change(C, imm <= x);
                set_flags(N | Z, imm = x - imm);
                break;
            }

            case 0xC0: { // CPY
                unsigned char imm = read_pc_inc(bus);
                flag_change(C, imm <= y);
                set_flags(N | Z, imm = y - imm);
                break;
            }

            case 0x45: { // EOR
                unsigned char zpg = read_pc_inc(bus);
                set_flags(N | Z, a = a ^ bus.read(zpg));
                break;
            }

            case 0x49: { // EOR
                unsigned char imm = read_pc_inc(bus);
                set_flags(N | Z, a = a ^ imm);
                break;
            }

            case 0x51: { // EOR
                unsigned char zpg = read_pc_inc(bus);
                int addr = bus.read(zpg) + bus.read(zpg + 1) * 256 + y;
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0xD1: { // CMP
                unsigned char zpg = read_pc_inc(bus);
                int addr = bus.read(zpg) + bus.read(zpg + 1) * 256 + y;
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xC5: { // CMP
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xCD: { // CMP
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xC9: { // CMP
                unsigned char imm = read_pc_inc(bus);
                flag_change(C, imm <= a);
                set_flags(N | Z, imm = a - imm);
                break;
            }

            case 0xD5: { // CMP
                unsigned char zpg = read_pc_inc(bus) + x;
                m = bus.read(zpg);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }
            
            case 0xE4: { // CPX
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                flag_change(C, m <= x);
                set_flags(N | Z, m = x - m);
                break;
            }

            case 0xC4: { // CPY
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                flag_change(C, m <= y);
                set_flags(N | Z, m = y - m);
                break;
            }

            case 0x90: { // BCC
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(!isset(C))
                    pc += rel;
                break;
            }

            case 0xB0: { // BCS
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(isset(C))
                    pc += rel;
                break;
            }

            case 0xD0: { // BNE
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(!isset(Z))
                    pc += rel;
                break;
            }

            case 0xF0: { // BEQ
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(isset(Z))
                    pc += rel;
                break;
            }

            case 0x85: { // STA
                unsigned char zpg = read_pc_inc(bus);
                bus.write(zpg, a);
                break;
            }

            case 0x60: { // RTS
                unsigned char pcl = stack_pull(bus);
                unsigned char pch = stack_pull(bus);
                pc = pcl + pch * 256 + 1;
                break;
            }

            case 0x95: { // STA
                unsigned char zpg = read_pc_inc(bus);
                bus.write((zpg + x) % 0x100, a);
                break;
            }

            case 0x94: { // STY
                unsigned char zpg = read_pc_inc(bus);
                bus.write((zpg + x) % 0x100, y);
                break;
            }

            case 0x86: { // STX
                unsigned char zpg = read_pc_inc(bus);
                bus.write(zpg, x);
                break;
            }

            case 0x84: { // STY
                unsigned char zpg = read_pc_inc(bus);
                bus.write(zpg, y);
                break;
            }

            case 0x8C: { // STY
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                bus.write(addr, y);
                break;
            }

            case 0x20: { // JSR
                stack_push(bus, (pc + 1) >> 8);
                stack_push(bus, (pc + 1) & 0xFF);
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                pc = addr;
                break;
            }

            default:
                printf("unhandled instruction %02X\n", inst);
                exit(1);
        }
        if(debug & DEBUG_STATE) {
            unsigned char s0 = bus.read(0x100 + s + 0);
            unsigned char s1 = bus.read(0x100 + s + 1);
            unsigned char s2 = bus.read(0x100 + s + 2);
            unsigned char pc0 = bus.read(pc + 0);
            unsigned char pc1 = bus.read(pc + 1);
            unsigned char pc2 = bus.read(pc + 2);
            printf("6502: A:%02X X:%02X Y:%02X P:", a, x, y);
            printf("%s", (p & N) ? "N" : "n");
            printf("%s", (p & V) ? "V" : "v");
            printf("-");
            printf("%s", (p & B) ? "B" : "b");
            printf("%s", (p & D) ? "D" : "d");
            printf("%s", (p & I) ? "I" : "i");
            printf("%s", (p & Z) ? "Z" : "z");
            printf("%s ", (p & C) ? "C" : "c");
            printf("S:%02X (%02X %02X %02X ...) PC:%04X (%02X %02X %02X ...)\n", s, s0, s1, s2, pc, pc0, pc1, pc2);
        }
    }
};


void usage(char *progname)
{
    printf("\n");
    printf("usage: %s ROM.bin\n", progname);
    printf("\n");
    printf("\n");
}

void go_verbose(int)
{
    debug = DEBUG_ERROR | DEBUG_DECODE | DEBUG_STATE | DEBUG_RW;

}

int main(int argc, char **argv)
{
    char *progname = argv[0];
    argc -= 1;
    argv += 1;

    while((argc > 0) && (argv[0][0] == '-')) {
	if(
            (strcmp(argv[0], "-help") == 0) ||
            (strcmp(argv[0], "-h") == 0) ||
            (strcmp(argv[0], "-?") == 0))
        {
            usage(progname);
            exit(EXIT_SUCCESS);
	} else {
	    fprintf(stderr, "unknown parameter \"%s\"\n", argv[0]);
            usage(progname);
	    exit(EXIT_FAILURE);
	}
    }

    if(argc < 1) {
        usage(progname);
        exit(EXIT_FAILURE);
    }

    char *romname = argv[0];
    unsigned char b[32768];

    FILE *fp = fopen(romname, "rb");
    if(fp == NULL) {
        fprintf(stderr, "failed to open %s for reading\n", romname);
        exit(EXIT_FAILURE);
    }
    size_t length = fread(b, 1, sizeof(b), fp);
    if(length < rom_region.size) {
        fprintf(stderr, "ROM read from %s was unexpectedly short (%zd bytes)\n", romname, length);
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    bus.boards.push_back(new MAINboard(b));

    for(auto b = bus.boards.begin(); b != bus.boards.end(); b++) {
        (*b)->reset();
    }

    signal(SIGUSR1, go_verbose);

    CPU6502 cpu;

    bool debugging = false;

    while(1) {
        if(!debugging) {
            poll_keyboard();

            char key;
            bool have_key = peek_key(&key);

            if(have_key && (key == '')) {
                debugging = true;
                clear_strobe();
                stop_keyboard();
                get_any_key_down_and_clear_strobe();
                continue;
            }

            chrono::time_point<chrono::system_clock> then;
            for(int i = 0; i < 25575; i++) // ~ 1/10th second
                cpu.cycle(bus);
            chrono::time_point<chrono::system_clock> now;

            auto elapsed_millis = chrono::duration_cast<chrono::milliseconds>(now - then);
            this_thread::sleep_for(chrono::milliseconds(100) - elapsed_millis);

        } else {

            printf("> ");
            char line[512];
            fgets(line, sizeof(line) - 1, stdin);
            line[strlen(line) - 1] = '\0';
            if(strcmp(line, "go") == 0) {
                printf("continuing\n");
                debugging = false;
                start_keyboard();
                continue;
            } else if(strncmp(line, "debug", 5) == 0) {
                sscanf(line + 6, "%d", &debug);
                printf("debug set to %02X\n", debug);
            }

            cpu.cycle(bus);
        }
    }
}
