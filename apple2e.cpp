#include <cstdlib>

#include "emulator.h"

const unsigned int DEBUG_RW = 0x01;
const unsigned int DEBUG_DECODE = 0x02;
const unsigned int DEBUG_STATE = 0x04;
const unsigned int DEBUG_ERROR = 0x08;
unsigned int debug = DEBUG_ERROR | DEBUG_DECODE | DEBUG_STATE;

struct MAINboard : board_base
{
    static const int text1_base = 0x400;
    static const int text1_size = 0x3FF;
    static const int text2_base = 0x800;
    static const int text2_size = 0xBFF;
    static const int io_base = 0xC000; 
    static const int io_size = 0x0100; 
    static const int irom_base = 0xC100; 
    static const int irom_size = 0x0F00; 
    static const int rom_base = 0xD000; 
    static const int rom_size = 0x3000; 
    unsigned char rom_bytes[rom_size];
    unsigned char irom_bytes[irom_size];
    unsigned char ram_bytes[65536];
    bool ROM_is_active;
    bool CXROM_is_active;
    MAINboard(unsigned char rom[32768]) :
        ROM_is_active(true),
        CXROM_is_active(true)
    {
        memcpy(rom_bytes, rom + rom_base - 0x8000 , sizeof(rom_bytes));
        memcpy(irom_bytes, rom + irom_base - 0x8000 , sizeof(irom_bytes));
        memset(ram_bytes, 0x76, sizeof(ram_bytes));
    }
    static const int RDCXROM = 0xC015;
    static const int SETINTCXROM = 0xC007;
    static const int TXTSET = 0xC051;
    static const int TXTPAGE1 = 0xC054;
    static const int LORES = 0xC056;
    virtual bool read(int addr, unsigned char &data)
    {
        if(debug & DEBUG_RW) printf("MAIN board read\n");
        if(addr >= io_base && addr < io_base + io_size) {
            switch(addr) {
                case RDCXROM:
                    printf("RD CXROM\n");
                    data = CXROM_is_active ? 0x80 : 0x00;
                    return true;
                    break;
                case TXTSET:
                    printf("RD TXTSET\n");
                    break;
                case TXTPAGE1:
                    printf("RD TXTPAGE1\n");
                    break;
                case LORES:
                    printf("RD LORES\n");
                    break;
                default:
                    printf("unhandled MMIO RD %04X\n", addr);
                    exit(0);
                    break;
            }
        }
        if(ROM_is_active && addr >= irom_base && addr < irom_base + sizeof(irom_bytes)) {
            data = irom_bytes[addr - irom_base];
            if(debug & DEBUG_RW) printf("read 0x%04X -> 0x%02X from internal ROM\n", addr, data);
            return true;
        }
        if(ROM_is_active && addr >= rom_base && addr < rom_base + sizeof(rom_bytes)) {
            data = rom_bytes[addr - rom_base];
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
        if(addr >= text1_base && addr < text1_base + text1_size) {
            printf("TEXT1 WRITE!\n");
            exit(0);
        }
        if(addr >= text2_base && addr < text2_base + text2_size) {
            printf("TEXT2 WRITE!\n");
            exit(0);
        }
        if(addr >= io_base && addr < io_base + io_size) {
            switch(addr) {
                case SETINTCXROM:
                    printf("WR SETINTCXROM\n");
                    CXROM_is_active = true;
                case TXTSET:
                    printf("WR TXTSET\n");
                    break;
                case TXTPAGE1:
                    printf("WR TXTPAGE1\n");
                    break;
                case LORES:
                    printf("WR LORES\n");
                    break;
                default:
                    printf("unhandled MMIO %04X write\n", addr);
                    exit(0);
            }
        }
        if(ROM_is_active && addr >= rom_base && addr < rom_base + sizeof(rom_bytes)) {
            return false;
        }
        if(ROM_is_active && addr >= irom_base && addr < irom_base + sizeof(irom_bytes)) {
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
            if((*b)->read(addr, data))
                return data;
        }
        if(debug & DEBUG_ERROR)
            fprintf(stderr, "no ownership of read at %04X\n", addr);
        return 0xAA;
    }
    void write(int addr, unsigned char data)
    {
        for(auto b = boards.begin(); b != boards.end(); b++) {
            if((*b)->write(addr, data))
                return;
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
    void cycle(bus_controller& bus)
    {
        if(exception == RESET) {
            if(debug & DEBUG_STATE) printf("RESET\n");
            reset(bus);
        }

        unsigned char inst = read_pc_inc(bus);

        switch(inst) {
            case 0xD8: {
                if(debug & DEBUG_DECODE) printf("CLD\n");
                flag_clear(D);
                break;
            }

            case 0xCE: {
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                if(debug & DEBUG_DECODE) printf("DEC %02X, Y\n", addr);
                unsigned char m = bus.read(addr) - 1;
                flag_change(Z, m == 0);
                flag_change(N, m & 0x80);
                bus.write(addr, m);
                break;
            }

            case 0x30: {
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(debug & DEBUG_DECODE) printf("BMI %02X\n", rel);
                if(isset(N))
                    pc += rel;
                break;
            }

            case 0xB9: {
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                if(debug & DEBUG_DECODE) printf("LDA %02X, Y\n", addr);
                a = bus.read(addr + y);
                flag_change(Z, a == 0x00);
                flag_change(N, a & 0x80);
                break;
            }

            case 0x69: {
                unsigned char imm = read_pc_inc(bus);
                if(debug & DEBUG_DECODE) printf("ADC %02X\n", imm);
                int carry = isset(C) ? 1 : 0;
                flag_change(C, (int)(a + imm + carry) > 0xFF);
                a = a + imm + carry;
                flag_change(N, a & 0x80);
                flag_change(V, isset(C) != isset(N));
                flag_change(Z, a == 0);
                break;
            }

            case 0x4A: {
                if(debug & DEBUG_DECODE) printf("LSR\n");
                flag_change(C, a & 0x01);
                a = a >> 1;
                flag_change(N, a & 0x80);
                flag_change(Z, a == 0);
                break;
            }

            case 0x68: {
                if(debug & DEBUG_DECODE) printf("PLA\n");
                a = stack_pull(bus);
                flag_change(Z, a == 0x00);
                flag_change(N, a & 0x80);
                break;
            }

            case 0x48: {
                if(debug & DEBUG_DECODE) printf("PHA\n");
                stack_push(bus, a);
                break;
            }

            case 0x09: {
                unsigned char imm = read_pc_inc(bus);
                if(debug & DEBUG_DECODE) printf("OR %02X\n", imm);
                a = a | imm;
                flag_change(Z, a == 0x00);
                flag_change(N, a & 0x80);
                break;
            }

            case 0x29: {
                unsigned char imm = read_pc_inc(bus);
                if(debug & DEBUG_DECODE) printf("AND %02X\n", imm);
                a = a & imm;
                flag_change(Z, a == 0x00);
                flag_change(N, a & 0x80);
                break;
            }

            case 0x88: {
                if(debug & DEBUG_DECODE) printf("DEY\n");
                y = y - 1;
                flag_change(Z, y == 0x00);
                flag_change(N, y & 0x80);
                break;
            }

            case 0x76: {
                unsigned char zpg = read_pc_inc(bus) + x;
                if(debug & DEBUG_DECODE) printf("ROR %02X\n", zpg);
                unsigned char m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                m = (c ? 0x80 : 0x00) | (m >> 1);
                flag_change(N, m & 0x80);
                flag_change(Z, m == 0);
                bus.write(zpg, m);
                break;
            }

            case 0x4C: {
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                if(debug & DEBUG_DECODE) printf("JMP %04X\n", addr);
                pc = addr;
                break;
            }

            case 0x8D: {
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                if(debug & DEBUG_DECODE) printf("STA %04X\n", addr);
                bus.write(addr, a);
                break;
            }

            case 0x08: {
                if(debug & DEBUG_DECODE) printf("PHP\n");
                stack_push(bus, p);
                break;
            }

            case 0x2C: {
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                if(debug & DEBUG_DECODE) printf("BIT #%04X\n", addr);
                unsigned char m = bus.read(addr);
                flag_change(Z, a & m);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x70);
                break;
            }

            case 0xA0: {
                unsigned char imm = read_pc_inc(bus);
                if(debug & DEBUG_DECODE) printf("LDY #%02X\n", imm);
                y = imm;
                flag_change(N, y & 0x80);
                flag_change(Z, y == 0);
                break;
            }

            case 0xA9: {
                unsigned char imm = read_pc_inc(bus);
                if(debug & DEBUG_DECODE) printf("LDA #%02X\n", imm);
                a = imm;
                flag_change(N, a & 0x80);
                flag_change(Z, a == 0);
                break;
            }

            case 0xAD: {
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                if(debug & DEBUG_DECODE) printf("LDA %04X\n", addr);
                a = bus.read(addr);
                flag_change(N, a & 0x80);
                flag_change(Z, a == 0);
                break;
            }

            case 0xD5: {
                unsigned char zpg = read_pc_inc(bus) + x;
                if(debug & DEBUG_DECODE) printf("CMP %02X\n", zpg);
                unsigned char m = bus.read(zpg);
                flag_change(N, m < a);
                m = a - m;
                flag_change(N, m & 0x80);
                flag_change(Z, m == 0);
                break;
            }

            case 0x90: {
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(debug & DEBUG_DECODE) printf("BCC %02X\n", rel);
                if(!isset(C))
                    pc += rel;
                break;
            }

            case 0xB0: {
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(debug & DEBUG_DECODE) printf("BCS %02X\n", rel);
                if(isset(C))
                    pc += rel;
                break;
            }

            case 0xD0: {
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(debug & DEBUG_DECODE) printf("BNE %02X\n", rel);
                if(!isset(Z))
                    pc += rel;
                break;
            }

            case 0xF0: {
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(debug & DEBUG_DECODE) printf("BEQ %02X\n", rel);
                if(isset(Z))
                    pc += rel;
                break;
            }

            case 0x85: {
                unsigned char zpg = read_pc_inc(bus);
                if(debug & DEBUG_DECODE) printf("STA %02X\n", zpg);
                bus.write(zpg, a);
                break;
            }

            case 0x60: {
                if(debug & DEBUG_DECODE) printf("RTS\n");
                unsigned char pcl = stack_pull(bus);
                unsigned char pch = stack_pull(bus);
                pc = pcl + pch * 256;
                break;
            }

            case 0x84: {
                unsigned char zpg = read_pc_inc(bus);
                if(debug & DEBUG_DECODE) printf("STY %02X\n", zpg);
                bus.write(zpg, y);
                break;
            }

            case 0x20: {
                stack_push(bus, (pc + 2) >> 8);
                stack_push(bus, (pc + 2) & 0xFF);
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                if(debug & DEBUG_DECODE) printf("JSR %04X\n", addr);
                pc = addr;
                break;
            }

            default:
                printf("unhandled instruction %02X\n", inst);
                exit(1);
        }
        if(debug & DEBUG_STATE) {
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
            printf("S:%02X PC:%04X (%02X %02X %02X)\n", s, pc, pc0, pc1, pc2);
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
    if(length < MAINboard::rom_size) {
        fprintf(stderr, "ROM read from %s was unexpectedly short (%zd bytes)\n", romname, length);
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    bus.boards.push_back(new MAINboard(b));

    for(auto b = bus.boards.begin(); b != bus.boards.end(); b++) {
        (*b)->reset();
    }

    CPU6502 cpu;

    while(1) {
        cpu.cycle(bus);
        printf("> ");
        char line[512];
        fgets(line, sizeof(line) - 1, stdin);
    }
}
