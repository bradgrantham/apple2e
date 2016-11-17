#include <cstdlib>
#include <cstring>
#include <string>
#include <set>
#include <chrono>
#include <thread>
#include <ratio>
#include <iostream>
#include <deque>
#include <map>
#include <thread>
#include <signal.h>


#include "fake6502.h"

const int rom_image_size = 0x3000;

using namespace std;

#include "emulator.h"
#include "keyboard.h"
#include "dis6502.h"
#include "interface.h"

const unsigned int DEBUG_ERROR = 0x01;
const unsigned int DEBUG_WARN = 0x02;
const unsigned int DEBUG_DECODE = 0x04;
const unsigned int DEBUG_STATE = 0x08;
const unsigned int DEBUG_RW = 0x10;
const unsigned int DEBUG_BUS = 0x20;
volatile unsigned int debug = DEBUG_ERROR | DEBUG_WARN ; // | DEBUG_DECODE | DEBUG_STATE | DEBUG_RW;

volatile bool exit_on_banking = false;
volatile bool exit_on_memory_fallthrough = true;

struct SoftSwitch
{
    string name;
    int clear_address;
    int set_address;
    int read_address;
    bool read_also_changes;
    bool enabled = false;
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
        return enabled;
    }
};

struct APPLE2Edisplay
{
    static constexpr int text_row_base_offsets[] = 
    {
        0x000,
        0x080,
        0x100,
        0x180,
        0x200,
        0x280,
        0x300,
        0x380,
        0x028,
        0x0A8,
        0x128,
        0x1A8,
        0x228,
        0x2A8,
        0x328,
        0x3A8,
        0x050,
        0x0D0,
        0x150,
        0x1D0,
        0x250,
        0x2D0,
        0x350,
        0x3D0,
    };

    // External display switches
    enum {TEXT, LORES, HIRES} mode = TEXT;
    int display_page = 0; // Apple //e page minus 1 (so 0,1 not 1,2)
    bool mixed_mode = false;

    bool changed = false;
    unsigned char text_page[2][24][40];

    void display_text(int page)
    {
        printf("------------------------------------------\n");
        for(int row = 0; row < 24; row++) {
            printf("|");
            for(int col = 0; col < 40; col++) {
                int ch = text_page[page][row][col] & 0x7F;
                printf("%c", isprint(ch) ? ch : '?');
            }
            printf("|\n");
        }
        printf("------------------------------------------\n");
    }

    void display()
    {
        // enum {TEXT, LORES, HIRES} mode = TEXT;
        // int display_page = 0;
        // bool mixed_mode = false;
        if(mode == TEXT) {
            display_text(display_page);
        } else {
        }
    }

    static const int text_page1_base = 0x400;
    static const int text_page2_base = 0x800;
    static const int text_page_size = 0x400;
    bool write(int addr, unsigned char data)
    {
        // We know text page 1 and 2 are contiguous
        if((addr >= text_page1_base) && (addr < text_page2_base + text_page_size)) {
            int page = (addr >= text_page2_base) ? 1 : 0;
            int within_page = addr - text_page1_base - page * text_page_size;
            for(int row = 0; row < 24; row++) {
                int row_offset = text_row_base_offsets[row];
                if((within_page >= row_offset) && 
                    (within_page < row_offset + 40)){
                    int col = within_page - row_offset;
                    printf("TEXT WRITE %d %d %d '%c'\n", page, row, col, data);
                    text_page[page][row][col] = data;
                }
            }
            changed = true;
            return true;
        }
        return false;
    }
};
constexpr int APPLE2Edisplay::text_row_base_offsets[24];

struct region
{
    string name;
    int base;
    int size;
    bool contains(int addr) const
    {
        return addr >= base && addr < base + size;
    }
};

typedef std::function<bool()> enabled_func;

enum MemoryType {RAM, ROM};

struct backed_region : region
{
    vector<unsigned char> memory;
    MemoryType type;
    enabled_func read_enabled;
    enabled_func write_enabled;

    backed_region(const char* name, int base, int size, MemoryType type_, vector<backed_region*>& regions, enabled_func enabled_) :
        region{name, base, size},
        memory(size),
        type(type_),
        read_enabled(enabled_),
        write_enabled(enabled_)
    {
        std::fill(memory.begin(), memory.end(), 0x00);
        regions.push_back(this);
    }

    backed_region(const char* name, int base, int size, MemoryType type_, vector<backed_region*>& regions, enabled_func read_enabled_, enabled_func write_enabled_) :
        region{name, base, size},
        memory(size),
        type(type_),
        read_enabled(read_enabled_),
        write_enabled(write_enabled_)
    {
        std::fill(memory.begin(), memory.end(), 0x00);
        regions.push_back(this);
    }

    bool contains(int addr) const
    {
        return addr >= base && addr < base + size;
    }

    bool read(int addr, unsigned char& data)
    {
        if(contains(addr) && read_enabled()) {
            data = memory[addr - base];
            return true;
        }
        return false;
    }

    bool write(int addr, unsigned char data)
    {
        if((type == RAM) && contains(addr) && write_enabled()) {
            memory[addr - base] = data;
            return true;
        }
        return false;
    }
};

const region io_region = {"io", 0xC000, 0x100};

struct MAINboard : board_base
{
    vector<SoftSwitch*> switches;
    SoftSwitch CXROM {"CXROM", 0xC006, 0xC007, 0xC015, false, switches, true};
    SoftSwitch STORE80 {"STORE80", 0xC000, 0xC001, 0xC018, false, switches, true};
    SoftSwitch RAMRD {"RAMRD", 0xC002, 0xC003, 0xC013, false, switches};
    SoftSwitch RAMWRT {"RAMWRT", 0xC004, 0xC005, 0xC014, false, switches};
    SoftSwitch ALTZP {"ALTZP", 0xC008, 0xC009, 0xC016, false, switches};
    SoftSwitch C3ROM {"C3ROM", 0xC00A, 0xC00B, 0xC017, false, switches, true};
    SoftSwitch ALTCHAR {"ALTCHAR", 0xC00E, 0xC00F, 0xC01E, false, switches};
    SoftSwitch VID80 {"VID80", 0xC00C, 0xC00D, 0xC01F, false, switches};
    SoftSwitch TEXT {"TEXT", 0xC050, 0xC051, 0xC01A, true, switches, true};
    SoftSwitch MIXED {"MIXED", 0xC052, 0xC053, 0xC01B, true, switches, true};
    SoftSwitch PAGE2 {"PAGE2", 0xC054, 0xC055, 0xC01C, true, switches, true};
    SoftSwitch HIRES {"HIRES", 0xC056, 0xC057, 0xC01D, true, switches, true};

    vector<backed_region*> regions;
    backed_region szp = {"szp", 0x0000, 0x0200, RAM, regions, [&](){return !ALTZP;}}; // stack and zero page
    backed_region aszp = {"aszp", 0x0000, 0x0200, RAM, regions, [&](){return ALTZP;}}; // alternate stack and zero page

    backed_region rom_D000 = {"rom_D000", 0xD000, 0x1000, ROM, regions, [&]{return true;}};
    backed_region rom_E000 = {"rom_E000", 0xE000, 0x2000, ROM, regions, [&]{return true;}};

    bool internal_C800_ROM_selected;
    backed_region rom_C100 = {"rom_C100", 0xC100, 0x0200, ROM, regions, [&]{return CXROM;}};
    backed_region rom_C300 = {"rom_C300", 0xC300, 0x0100, ROM, regions, [&]{return CXROM || (!CXROM && !C3ROM);}};
    backed_region rom_C400 = {"rom_C400", 0xC300, 0x0400, ROM, regions, [&]{return CXROM;}};
    backed_region rom_C800 = {"rom_C800", 0xC800, 0x0800, ROM, regions, [&]{return CXROM || (!CXROM && !C3ROM && internal_C800_ROM_selected);}};
    backed_region rom_CXXX_default = {"rom_CXXX_default", 0xC100, 0x0F00, ROM, regions, [&]{return true;}};

    enabled_func read_from_aux_ram = [&]{return RAMRD;};
    enabled_func write_to_aux_ram = [&]{return RAMWRT;};
    enabled_func read_from_main_ram = [&]{return !read_from_aux_ram();};
    enabled_func write_to_main_ram = [&]{return !write_to_aux_ram();};

    backed_region ram_0200 = {"ram_0200", 0x0200, 0x0200, RAM, regions, read_from_main_ram, write_to_main_ram};
    backed_region ram_0200_x = {"ram_0200_x", 0x0200, 0x0200, RAM, regions, read_from_aux_ram, write_to_aux_ram};
    backed_region ram_0C00 = {"ram_0C00", 0x0C00, 0x1400, RAM, regions, read_from_main_ram, write_to_main_ram};
    backed_region ram_0C00_x = {"ram_0C00_x", 0x0C00, 0x1400, RAM, regions, read_from_aux_ram, write_to_aux_ram};
    backed_region ram_6000 = {"ram_6000", 0x6000, 0x6000, RAM, regions, read_from_main_ram, write_to_main_ram};
    backed_region ram_6000_x = {"ram_6000_x", 0x6000, 0x6000, RAM, regions, read_from_aux_ram, write_to_aux_ram};

    enabled_func read_from_aux_text1 = [&]{return RAMRD && ((!STORE80) || (STORE80 && PAGE2));};
    enabled_func write_to_aux_text1 = [&]{return RAMWRT && ((!STORE80) || (STORE80 && PAGE2));};
    enabled_func read_from_main_text1 = [&]{return !read_from_aux_text1();};
    enabled_func write_to_main_text1 = [&]{return !write_to_aux_text1();};

    backed_region text_page1 = {"text_page1", 0x0400, 0x0400, RAM, regions, read_from_main_text1, write_to_main_text1};
    backed_region text_page1x = {"text_page1x", 0x0400, 0x0400, RAM, regions, read_from_aux_text1, write_to_aux_text1};
    backed_region text_page2 = {"text_page2", 0x0800, 0x0400, RAM, regions, read_from_main_ram, write_to_main_ram};
    backed_region text_page2x = {"text_page2x", 0x0800, 0x0400, RAM, regions, read_from_aux_ram, write_to_aux_ram};

    enabled_func read_from_aux_hires1 = [&]{return HIRES && RAMRD && ((!STORE80) || (STORE80 && PAGE2));};
    enabled_func write_to_aux_hires1 = [&]{return HIRES && RAMWRT && ((!STORE80) || (STORE80 && PAGE2));};
    enabled_func read_from_main_hires1 = [&]{return !read_from_aux_hires1();};
    enabled_func write_to_main_hires1 = [&]{return !write_to_aux_hires1();};

    backed_region hires_page1 = {"hires_page1", 0x2000, 0x2000, RAM, regions, read_from_main_hires1, write_to_main_hires1};
    backed_region hires_page1x = {"hires_page1x", 0x2000, 0x2000, RAM, regions, read_from_aux_hires1, write_to_aux_hires1};
    backed_region hires_page2 = {"hires_page2", 0x4000, 0x2000, RAM, regions, read_from_main_ram, write_to_main_ram};
    backed_region hires_page2x = {"hires_page2x", 0x4000, 0x2000, RAM, regions, read_from_aux_ram, write_to_aux_ram};

    enum {BANK1, BANK2} C08X_bank;
    bool C08X_read_RAM;
    bool C08X_write_RAM;

    backed_region ram1_main_D000 = {"ram1_main_D000", 0xD000, 0x1000, RAM, regions, [&]{return !ALTZP && C08X_read_RAM && (C08X_bank == BANK1);}, [&]{return !ALTZP && C08X_write_RAM && (C08X_bank == BANK1);}};
    backed_region ram2_main_D000 = {"ram2_main_D000", 0xD000, 0x1000, RAM, regions, [&]{return !ALTZP && C08X_read_RAM && (C08X_bank == BANK2);}, [&]{return !ALTZP && C08X_write_RAM && (C08X_bank == BANK2);}};
    backed_region ram_main_E000 = {"ram1_main_E000", 0xE000, 0x2000, RAM, regions, [&]{return C08X_read_RAM;}, [&]{return !ALTZP && C08X_write_RAM;}};
    backed_region ram1_main_D000_x = {"ram1_main_D000_x", 0xD000, 0x1000, RAM, regions, [&]{return ALTZP && C08X_read_RAM && (C08X_bank == BANK1);}, [&]{return ALTZP && C08X_write_RAM && (C08X_bank == BANK1);}};
    backed_region ram2_main_D000_x = {"ram2_main_D000_x", 0xD000, 0x1000, RAM, regions, [&]{return ALTZP && C08X_read_RAM && (C08X_bank == BANK2);}, [&]{return ALTZP && C08X_write_RAM && (C08X_bank == BANK2);}};
    backed_region ram_main_E000_x = {"ram1_main_E000_x", 0xE000, 0x2000, RAM, regions, [&]{return C08X_read_RAM;}, [&]{return ALTZP && C08X_write_RAM;}};

    set<int> ignore_mmio = {0xC058, 0xC05A, 0xC05D, 0xC05F, 0xC061, 0xC062};
    set<int> banking_read_switches = {
        0xC080, 0xC081, 0xC082, 0xC083, 0xC084, 0xC085, 0xC086, 0xC087,
        0xC088, 0xC089, 0xC08A, 0xC08B, 0xC08C, 0xC08D, 0xC08E, 0xC08F,
    };
    set<int> banking_write_switches = {
        0xC006, 0xC007,
        0xC000, 0xC001,
        0xC002, 0xC003,
        0xC004, 0xC005,
        0xC008, 0xC009,
        0xC00A, 0xC00B,
    };

    deque<unsigned char> keyboard_buffer;

    APPLE2Edisplay *display;

    void enqueue_key(unsigned char k)
    {
        keyboard_buffer.push_back(k);
    }

    MAINboard(unsigned char rom_image[32768], APPLE2Edisplay *d) :
        internal_C800_ROM_selected(true),
        display(d)
    {
        std::copy(rom_image + rom_D000.base - 0x8000, rom_image + rom_D000.base - 0x8000 + rom_D000.size, rom_D000.memory.begin());
        std::copy(rom_image + rom_E000.base - 0x8000, rom_image + rom_E000.base - 0x8000 + rom_E000.size, rom_E000.memory.begin());
        std::copy(rom_image + rom_C100.base - 0x8000, rom_image + rom_C100.base - 0x8000 + rom_C100.size, rom_C100.memory.begin());
        std::copy(rom_image + rom_C300.base - 0x8000, rom_image + rom_C300.base - 0x8000 + rom_C300.size, rom_C300.memory.begin());
        std::copy(rom_image + rom_C400.base - 0x8000, rom_image + rom_C400.base - 0x8000 + rom_C400.size, rom_C400.memory.begin());
        std::copy(rom_image + rom_C800.base - 0x8000, rom_image + rom_C800.base - 0x8000 + rom_C800.size, rom_C800.memory.begin());
    }

    virtual ~MAINboard()
    {
    }

    void reset()
    {
        // Partially from Apple //e Technical Reference
        // XXX need to double-check these against the actual hardware
        ALTZP.enabled = false;
        CXROM.enabled = false;
        RAMRD.enabled = false;
        RAMWRT.enabled = false;
        C3ROM.enabled = false;
        C08X_bank = BANK2;
        C08X_read_RAM = false;
        C08X_write_RAM = true;
        internal_C800_ROM_selected = true;
    }

    virtual bool read(int addr, unsigned char &data)
    {
        if(debug & DEBUG_RW) printf("MAIN board read\n");
        if(io_region.contains(addr)) {
            if(exit_on_banking && (banking_read_switches.find(addr) != banking_read_switches.end())) {
                printf("bank switch control %04X, aborting\n", addr);
                exit(1);
            }
            for(auto it = switches.begin(); it != switches.end(); it++) {
                SoftSwitch* sw = *it;
                if(addr == sw->read_address) {
                    data = sw->enabled ? 0x80 : 0x00;
                    if(debug & DEBUG_RW) printf("Read status of %s = %02X\n", sw->name.c_str(), data);
                    return true;
                } else if(sw->read_also_changes && addr == sw->set_address) {
                    if(!sw->implemented) { printf("%s ; set is unimplemented\n", sw->name.c_str()); fflush(stdout); exit(0); }
                    data = 0xff;
                    sw->enabled = true;
                    if(debug & DEBUG_RW) printf("Set %s\n", sw->name.c_str());
                    return true;
                } else if(sw->read_also_changes && addr == sw->clear_address) {
                    if(!sw->implemented) { printf("%s ; unimplemented\n", sw->name.c_str()); fflush(stdout); exit(0); }
                    data = 0xff;
                    sw->enabled = false;
                    if(debug & DEBUG_RW) printf("Clear %s\n", sw->name.c_str());
                    return true;
                }
            }
            if(ignore_mmio.find(addr) != ignore_mmio.end()) {
                if(debug & DEBUG_RW) printf("read %04X, ignored, return 0x00\n", addr);
                data = 0x00;
                return true;
            }
            if((addr & 0xFFF0) == 0xC080) {
                C08X_bank = ((addr >> 3) & 1) ? BANK1 : BANK2;
                C08X_write_RAM = (addr >> 2) & 1;
                C08X_read_RAM = ((addr >> 1) & 1) ^ C08X_write_RAM; // RAM flag is inverted by ROM!
                if(debug & DEBUG_RW) printf("write C08X switch, return 0x%02X\n", data);
                return true;
            }
            if(addr == 0xC011) {
                data = (C08X_bank == BANK2) ? 0x80 : 0x0;
                if(debug & DEBUG_RW) printf("read BSRBANK2, return 0x%02X\n", data);
                return true;
            }
            if(addr == 0xC012) {
                data = C08X_read_RAM ? 0x80 : 0x0;
                if(debug & DEBUG_RW) printf("read BSRREADRAM, return 0x%02X\n", data);
                return true;
            }
            if(addr == 0xC000) {
                if(!keyboard_buffer.empty()) {
                    data = 0x80 | keyboard_buffer[0];
                } else {
                    data = 0x00;
                }
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
                if(!keyboard_buffer.empty()) {
                    keyboard_buffer.pop_front();
                }
                data = 0x0;
                if(debug & DEBUG_RW) printf("read KBDSTRB, return 0x%02X\n", data);
                return true;
            }
            printf("unhandled MMIO Read at %04X\n", addr);
            fflush(stdout); exit(0);
        }
        for(auto it = regions.begin(); it != regions.end(); it++) {
            backed_region* r = *it;
            if(r->read(addr, data)) {
                if(debug & DEBUG_RW) printf("read 0x%04X -> 0x%02X from %s\n", addr, data, r->name.c_str());
                return true;
            }
        }
        if((addr & 0xFF00) == 0xC300) {
            if(debug & DEBUG_RW) printf("read 0x%04X, enabling internal C800 ROM\n", addr);
            internal_C800_ROM_selected = true;
        }
        if(addr == 0xCFFF) {
            if(debug & DEBUG_RW) printf("read 0xCFFF, disabling internal C800 ROM\n");
            internal_C800_ROM_selected = false;
        }
        if(debug & DEBUG_WARN) printf("unhandled memory read at %04X\n", addr);
        if(exit_on_memory_fallthrough) {
            printf("unhandled memory read at %04X, aborting\n", addr);
            printf("Switches:\n");
            for(auto it = switches.begin(); it != switches.end(); it++) {
                SoftSwitch* sw = *it;
                printf("    %s: %s\n", sw->name.c_str(), sw->enabled ? "enabled" : "disabled");
            }
            exit(1);
        }
        return false;
    }
    virtual bool write(int addr, unsigned char data)
    {
        if(text_page1.write(addr, data) ||
            text_page1x.write(addr, data) ||
            text_page2.write(addr, data) ||
            text_page2x.write(addr, data) ||
            hires_page1.write(addr, data) ||
            hires_page1x.write(addr, data) ||
            hires_page2.write(addr, data) ||
            hires_page2x.write(addr, data))
        {
            display->write(addr, data);
            return true;
        }
        if(io_region.contains(addr)) {
            if(exit_on_banking && (banking_write_switches.find(addr) != banking_write_switches.end())) {
                printf("bank switch control %04X, exiting\n", addr);
                exit(1);
            }
            for(auto it = switches.begin(); it != switches.end(); it++) {
                SoftSwitch* sw = *it;
                if(addr == sw->set_address) {
                    if(!sw->implemented) { printf("%s ; set is unimplemented\n", sw->name.c_str()); fflush(stdout); exit(0); }
                    data = 0xff;
                    sw->enabled = true;
                    if(debug & DEBUG_RW) printf("Set %s\n", sw->name.c_str());
                    return true;
                } else if(addr == sw->clear_address) {
                    // if(!sw->implemented) { printf("%s ; unimplemented\n", sw->name.c_str()); fflush(stdout); exit(0); }
                    data = 0xff;
                    sw->enabled = false;
                    if(debug & DEBUG_RW) printf("Clear %s\n", sw->name.c_str());
                    return true;
                }
            }
            if(addr == 0xC010) {
                if(debug & DEBUG_RW) printf("write KBDSTRB\n");
                if(!keyboard_buffer.empty()) {
                    keyboard_buffer.pop_front();
                }
                // reset keyboard latch
                return true;
            }
            if(addr == 0xC030) {
                if(debug & DEBUG_RW) printf("write SPKR\n");
                // click
                return true;
            }
            printf("unhandled MMIO Write at %04X\n", addr);
            fflush(stdout); exit(0);
        }
        for(auto it = regions.begin(); it != regions.end(); it++) {
            backed_region* r = *it;
            if(r->write(addr, data)) {
                if(debug & DEBUG_RW) printf("wrote %02X to 0x%04X in %s\n", addr, data, r->name.c_str());
                return true;
            }
        }
        if(debug & DEBUG_WARN) printf("unhandled memory write to %04X\n", addr);
        if(exit_on_memory_fallthrough) {
            printf("unhandled memory write to %04X, exiting\n", addr);
            exit(1);
        }
        return false;
    }
};

struct bus_controller
{
    vector<board_base*> boards;
    map<int, vector<unsigned char> > writes;
    map<int, vector<unsigned char> > reads;
    unsigned char read(int addr)
    {
        for(auto b = boards.begin(); b != boards.end(); b++) {
            unsigned char data = 0xaa;
            if((*b)->read(addr, data)) {
                if(debug & DEBUG_BUS) printf("read %04X returned %02X\n", addr, data);
                // reads[addr].push_back(data);
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
                // writes[addr].push_back(data);
                return;
            }
        }
        if(debug & DEBUG_ERROR)
            fprintf(stderr, "no ownership of write %02X at %04X\n", data, addr);
    }

    void reset()
    {
        for(auto b = boards.begin(); b != boards.end(); b++) {
            (*b)->reset();
        }
    }
};

bus_controller bus;

extern "C" {

uint8_t read6502(uint16_t address) 
{
    return bus.read(address);
}

void write6502(uint16_t address, uint8_t value)
{
    bus.write(address, value);
}

};

bool sbc_overflow(unsigned char a, unsigned char b, int borrow)
{
    signed char a_ = a;
    signed char b_ = b;
    signed short c = a_ - (b_ + borrow);
    return (c < -128) || (c > 127);
}

bool adc_overflow(unsigned char a, unsigned char b, int carry)
{
    signed char a_ = a;
    signed char b_ = b;
    signed short c = a_ + b_ + carry;
    return (c < -128) || (c > 127);
}

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
        p(0x20),
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
    void irq(bus_controller& bus)
    {
        stack_push(bus, (pc + 0) >> 8);
        stack_push(bus, (pc + 0) & 0xFF);
        stack_push(bus, p);
        pc = bus.read(0xFFFE) + bus.read(0xFFFF) * 256;
        exception = NONE;
    }
    void brk(bus_controller& bus)
    {
        stack_push(bus, (pc - 1) >> 8);
        stack_push(bus, (pc - 1) & 0xFF);
        stack_push(bus, p | B); // | B says the Synertek 6502 reference
        pc = bus.read(0xFFFE) + bus.read(0xFFFF) * 256;
        exception = NONE;
    }
    void nmi(bus_controller& bus)
    {
        stack_push(bus, (pc + 0) >> 8);
        stack_push(bus, (pc + 0) & 0xFF);
        stack_push(bus, p);
        pc = bus.read(0xFFFA) + bus.read(0xFFFB) * 256;
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
        } if(exception == NMI) {
            if(debug & DEBUG_STATE) printf("NMI\n");
            nmi(bus);
        } if(exception == INT) {
            if(debug & DEBUG_STATE) printf("INT\n");
            irq(bus);
        }
        // BRK is a special case caused directly by an instruction

        unsigned char inst = read_pc_inc(bus);

        unsigned char m;

        switch(inst) {
            case 0x00: { // BRK
                brk(bus);
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

            case 0xC6: { // DEC zpg
                int zpg = read_pc_inc(bus);
                set_flags(N | Z, m = bus.read(zpg) - 1);
                bus.write(zpg, m);
                break;
            }

            case 0xCE: { // DEC abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, m = bus.read(addr) - 1);
                bus.write(addr, m);
                break;
            }

            case 0xCA: { // DEX
                set_flags(N | Z, x = x - 1);
                break;
            }

            case 0xE6: { // INC zpg
                int zpg = read_pc_inc(bus);
                set_flags(N | Z, m = bus.read(zpg) + 1);
                bus.write(zpg, m);
                break;
            }

            case 0xF6: { // INC zpg, X
                int zpg = (read_pc_inc(bus) + x) & 0xFF;
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

            case 0xA1: { // LDA (ind, X)
                unsigned char zpg = (read_pc_inc(bus) + x) & 0xFF;
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xB5: { // LDA zpg, X
                unsigned char zpg = read_pc_inc(bus);
                int addr = zpg + x;
                set_flags(N | Z, a = bus.read(addr & 0xFF));
                break;
            }

            case 0xB1: { // LDA ind, Y
                unsigned char zpg = read_pc_inc(bus);
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xA5: { // LDA zpg
                unsigned char zpg = read_pc_inc(bus);
                set_flags(N | Z, a = bus.read(zpg));
                break;
            }

            case 0xDD: { // CMP abs, X
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr + x);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xD9: { // CMP abs, Y
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr + y);
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xB9: { // LDA abs, Y
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, a = bus.read(addr + y));
                break;
            }

            case 0xBD: { // LDA abs, X
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, a = bus.read(addr + x));
                break;
            }

            case 0xF5: { // SBC zpg, X
                unsigned char zpg = (read_pc_inc(bus) + x) & 0xFF;
                m = bus.read(zpg);
                int borrow = isset(C) ? 0 : 1;
                flag_change(C, !(a < (m + borrow)));
                flag_change(V, sbc_overflow(a, m, borrow));
                set_flags(N | Z, a = a - (m + borrow));
                break;
            }

            case 0xE5: { // SBC zpg
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                int borrow = isset(C) ? 0 : 1;
                flag_change(C, !(a < (m + borrow)));
                flag_change(V, sbc_overflow(a, m, borrow));
                set_flags(N | Z, a = a - (m + borrow));
                break;
            }

            case 0xF1: { // SBC ind, Y
                unsigned char zpg = read_pc_inc(bus);
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xff) * 256 + y;
                m = bus.read(addr);
                int borrow = isset(C) ? 0 : 1;
                flag_change(C, !(a < (m + borrow)));
                flag_change(V, sbc_overflow(a, m, borrow));
                set_flags(N | Z, a = a - (m + borrow));
                break;
            }

            case 0xFD: { // SBC abs, X
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256 + x;
                unsigned char m = bus.read(addr);
                int borrow = isset(C) ? 0 : 1;
                flag_change(C, !(a < (m + borrow)));
                flag_change(V, sbc_overflow(a, m, borrow));
                set_flags(N | Z, a = a - (m + borrow));
                break;
            }

            case 0xED: { // SBC abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                unsigned char m = bus.read(addr);
                int borrow = isset(C) ? 0 : 1;
                flag_change(C, !(a < (m + borrow)));
                flag_change(V, sbc_overflow(a, m, borrow));
                set_flags(N | Z, a = a - (m + borrow));
                break;
            }

            case 0xE9: { // SBC imm
                unsigned char m = read_pc_inc(bus);
                int borrow = isset(C) ? 0 : 1;
                flag_change(C, !(a < (m + borrow)));
                flag_change(V, sbc_overflow(a, m, borrow));
                set_flags(N | Z, a = a - (m + borrow));
                break;
            }

            case 0x65: { // ADC
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                int carry = isset(C) ? 1 : 0;
                flag_change(C, (int)(a + m + carry) > 0xFF);
                flag_change(V, adc_overflow(a, m, carry));
                set_flags(N | Z, a = a + m + carry);
                break;
            }

            case 0x79: { // ADC abs, Y
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256 + y;
                m = bus.read(addr);
                int carry = isset(C) ? 1 : 0;
                flag_change(C, (int)(a + m + carry) > 0xFF);
                flag_change(V, adc_overflow(a, m, carry));
                set_flags(N | Z, a = a + m + carry);
                break;
            }

            case 0x69: { // ADC
                m = read_pc_inc(bus);
                int carry = isset(C) ? 1 : 0;
                flag_change(C, (int)(a + m + carry) > 0xFF);
                flag_change(V, adc_overflow(a, m, carry));
                set_flags(N | Z, a = a + m + carry);
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

            case 0x16: { // ASL
                unsigned char zpg = read_pc_inc(bus);
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

            case 0x46: { // LSR
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                bus.write(zpg, m);
                break;
            }

            case 0x56: { // LSR zpg, X
                unsigned char zpg = read_pc_inc(bus) + x;
                m = bus.read(zpg & 0xFF);
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

            case 0x2D: { // AND
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, a = a & bus.read(addr));
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

            case 0x66: { // ROR
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                bus.write(zpg, m);
                break;
            }

            case 0x76: { // ROR
                unsigned char zpg = (read_pc_inc(bus) + x) & 0xFF;
                m = bus.read(zpg);
                bool c = isset(C);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = (c ? 0x80 : 0x00) | (m >> 1));
                bus.write(zpg, m);
                break;
            }

            case 0x26: { // ROL
                unsigned char zpg = read_pc_inc(bus);
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
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
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
                flag_change(Z, (a & m) == 0);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x40);
                break;
            }

            case 0x2C: { // BIT
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr);
                flag_change(Z, (a & m) == 0);
                flag_change(N, m & 0x80);
                flag_change(V, m & 0x40);
                break;
            }

            case 0xB4: { // LDY
                unsigned char zpg = read_pc_inc(bus);
                set_flags(N | Z, y = bus.read((zpg + x) & 0xFF));
                break;
            }

            case 0xBE: { // LDX
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256 + y;
                set_flags(N | Z, x = bus.read(addr));
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

            case 0xAD: { // LDA
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, a = bus.read(addr));
                break;
            }

            case 0xCC: { // CPY
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr);
                flag_change(C, m <= y);
                set_flags(N | Z, m = y - m);
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
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0xD1: { // CMP
                unsigned char zpg = read_pc_inc(bus);
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
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
                m = bus.read(zpg & 0xFF);
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
                bus.write((zpg + x) & 0xFF, a);
                break;
            }

            case 0x94: { // STY
                unsigned char zpg = read_pc_inc(bus);
                bus.write((zpg + x) & 0xFF, y);
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
                fflush(stdout); exit(1);
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
    printf("usage: %s [-debugger] ROM.bin\n", progname);
    printf("\n");
    printf("\n");
}

bool debugging = false;

void cleanup(void)
{
    if(!debugging) {
        stop_keyboard();
    }
    fflush(stdout);
    fflush(stderr);
}

bool use_fake6502 = false;

string read_bus_and_disassemble(bus_controller &bus, int pc)
{
    int bytes;
    string dis;
    unsigned char buf[4];
    buf[0] = bus.read(pc + 0);
    buf[1] = bus.read(pc + 1);
    buf[2] = bus.read(pc + 2);
    buf[3] = bus.read(pc + 3);
    tie(bytes, dis) = disassemble_6502(pc - 1, buf);
    return dis;
}

int millis_per_slice = 16;

struct key_to_ascii
{
    unsigned char no_shift_no_control;
    unsigned char yes_shift_no_control;
    unsigned char no_shift_yes_control;
    unsigned char yes_shift_yes_control;
};

map<int, key_to_ascii> interface_key_to_apple2e = 
{
    {'A', {97, 65, 1, 1}},
    {'B', {98, 66, 2, 2}},
    {'C', {99, 67, 3, 3}},
    {'D', {100, 68, 4, 4}},
    {'E', {101, 69, 5, 5}},
    {'F', {102, 70, 6, 6}},
    {'G', {103, 71, 7, 7}},
    {'H', {104, 72, 8, 8}},
    {'I', {105, 73, 9, 9}},
    {'J', {106, 74, 10, 10}},
    {'K', {107, 75, 11, 11}},
    {'L', {108, 76, 12, 12}},
    {'M', {109, 77, 13, 13}},
    {'N', {110, 78, 14, 14}},
    {'O', {111, 79, 15, 15}},
    {'P', {112, 80, 16, 16}},
    {'Q', {113, 81, 17, 17}},
    {'R', {114, 82, 18, 18}},
    {'S', {115, 83, 19, 19}},
    {'T', {116, 84, 20, 20}},
    {'U', {117, 85, 21, 21}},
    {'V', {118, 86, 22, 22}},
    {'W', {119, 87, 23, 23}},
    {'X', {120, 88, 24, 24}},
    {'Y', {121, 89, 25, 25}},
    {'Z', {122, 90, 26, 26}},
    {'1', {'1', '!', 0, 0}},
    {'2', {'2', '@', 0, 0}},
    {'3', {'3', '#', 0, 0}},
    {'4', {'4', '$', 0, 0}},
    {'5', {'5', '%', 0, 0}},
    {'6', {'6', '^', 0, 0}},
    {'7', {'7', '&', 0, 0}},
    {'8', {'8', '*', 0, 0}},
    {'9', {'9', '(', 0, 0}},
    {'0', {'0', ')', 0, 0}},
    {'-', {'-', '_', 0, 0}},
    {'=', {'=', '+', 0, 0}},
    {'[', {'[', '{', 0, 0}},
    {']', {']', '}', 0, 0}},
    {'\\', {'\\', '|', 0, 0}},
    {';', {';', ':', 0, 0}},
    {'\'', {'\'', '"', 0, 0}},
    {',', {',', '<', 0, 0}},
    {'.', {'.', '>', 0, 0}},
    {'/', {'/', '?', 0, 0}},
    {'`', {'`', '~', 0, 0}},
    {' ', {' ', ' ', 0, 0}},
};

void keyboard_to_mainboard(MAINboard *board)
{
    static bool shift_down = false;
    static bool control_down = false;
    // skip CAPS for now

    if(interface_event_waiting()) {
        event e = interface_dequeue_event();
        if(e.type == event::KEYDOWN) {
            if((e.value == event::LEFT_SHIFT) || (e.value == event::RIGHT_SHIFT))
                shift_down = true;
            else if((e.value == event::LEFT_CONTROL) || (e.value == event::RIGHT_CONTROL))
                control_down = true;
            else if(e.value == event::ENTER) {
                board->enqueue_key(141 - 128);
            } else if(e.value == event::TAB) {
                board->enqueue_key('	');
            } else if(e.value == event::ESCAPE) {
                board->enqueue_key('');
            } else if(e.value == event::DELETE) {
                board->enqueue_key(255 - 128);
            } else if(e.value == event::RIGHT) {
                board->enqueue_key(149 - 128);
            } else if(e.value == event::LEFT) {
                board->enqueue_key(136 - 128);
            } else if(e.value == event::DOWN) {
                board->enqueue_key(138 - 128);
            } else if(e.value == event::UP) {
                board->enqueue_key(139 - 128);
            } else {
                auto it = interface_key_to_apple2e.find(e.value);
                if(it != interface_key_to_apple2e.end()) {
                    const key_to_ascii& k = (*it).second;
                    if(!shift_down && !control_down)
                        board->enqueue_key(k.no_shift_no_control);
                    else if(shift_down && !control_down)
                        board->enqueue_key(k.yes_shift_no_control);
                    else if(!shift_down && control_down)
                        board->enqueue_key(k.no_shift_yes_control);
                    else if(shift_down && control_down)
                        board->enqueue_key(k.yes_shift_yes_control);
                }
            }
        } else if(e.type == event::KEYUP) {
            if((e.value == event::LEFT_SHIFT) || (e.value == event::RIGHT_SHIFT))
                shift_down = false;
            else if((e.value == event::LEFT_CONTROL) || (e.value == event::RIGHT_CONTROL))
                control_down = false;
        }
    }
}


int main(int argc, char **argv)
{
    char *progname = argv[0];
    argc -= 1;
    argv += 1;

    atexit(cleanup);

    while((argc > 0) && (argv[0][0] == '-')) {
	if(strcmp(argv[0], "-debugger") == 0) {
            debugging = true;
            argv++;
            argc--;
	} else if(strcmp(argv[0], "-d") == 0) {
            debug = atoi(argv[1]);
            argv += 2;
            argc -= 2;
        } else if(
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
    if(length < rom_image_size) {
        fprintf(stderr, "ROM read from %s was unexpectedly short (%zd bytes)\n", romname, length);
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    APPLE2Edisplay display;
    MAINboard* mainboard;

    bus.boards.push_back(mainboard = new MAINboard(b, &display));
    bus.reset();

    CPU6502 cpu;

    if(!debugging) {
        start_keyboard();
    }

    if(use_fake6502)
        reset6502();

    interface_start();

    while(1) {
        if(!debugging) {
            poll_keyboard();

            char key;
            bool have_key = peek_key(&key);

            keyboard_to_mainboard(mainboard);

            if(have_key) {
                if(key == '') {
                    debugging = true;
                    clear_strobe();
                    stop_keyboard();
                    continue;
                } else {
                    mainboard->enqueue_key(key);
                    clear_strobe();
                }
            }

            chrono::time_point<chrono::system_clock> then;
            const int inst_per_slice = 255750 * millis_per_slice / 1000;
            for(int i = 0; i < inst_per_slice; i++) {
                string dis = read_bus_and_disassemble(bus, cpu.pc);
                if(debug & DEBUG_DECODE)
                    printf("%s\n", dis.c_str());
                if(use_fake6502)
                    step6502();
                else
                    cpu.cycle(bus);
            }
            if(display.changed) {
                display.display();
                display.changed = false;
            }
            interface_iterate();
            chrono::time_point<chrono::system_clock> now;

            auto elapsed_millis = chrono::duration_cast<chrono::milliseconds>(now - then);
            this_thread::sleep_for(chrono::milliseconds(millis_per_slice) - elapsed_millis);

        } else {

            printf("> ");
            char line[512];
            if(fgets(line, sizeof(line) - 1, stdin) == NULL) {
		exit(0);
	    }
            line[strlen(line) - 1] = '\0';
            if(strcmp(line, "go") == 0) {
                printf("continuing\n");
                debugging = false;
                start_keyboard();
                continue;
            } else if(strcmp(line, "banking") == 0) {
                printf("abort on any banking\n");
                exit_on_banking = true;
                continue;
            } else if(strncmp(line, "debug", 5) == 0) {
                sscanf(line + 6, "%d", &debug);
                printf("debug set to %02X\n", debug);
                continue;
            } else if(strcmp(line, "reset") == 0) {
                printf("machine reset.\n");
                bus.reset();
                cpu.reset(bus);
                continue;
            } else if(strcmp(line, "reboot") == 0) {
                printf("CPU rebooted (NMI).\n");
                bus.reset();
                cpu.nmi(bus);
                continue;
            }
            string dis = read_bus_and_disassemble(bus, cpu.pc);
            if(debug & DEBUG_DECODE)
                printf("%s\n", dis.c_str());
            
            if(use_fake6502)
                step6502();
            else
                cpu.cycle(bus);
            if(display.changed) {
                display.display();
                display.changed = false;
            }
            interface_iterate();
        }
    }

    interface_shutdown();
}
