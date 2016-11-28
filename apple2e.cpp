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
#include <ao/ao.h>


#include "fake6502.h"

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
const unsigned int DEBUG_FLOPPY = 0x40;
const unsigned int DEBUG_SWITCH = 0x80;
volatile unsigned int debug = DEBUG_ERROR | DEBUG_WARN ; // | DEBUG_DECODE | DEBUG_STATE | DEBUG_RW;

volatile bool exit_on_banking = false;
volatile bool exit_on_memory_fallthrough = true;
volatile bool run_fast = false;
volatile bool pause_cpu = false;

typedef unsigned long long clk_t;
struct system_clock
{
    clk_t value = 0;
    operator clk_t() const { return value; }
    clk_t operator+=(clk_t i) { return value += i; }
    clk_t operator++(int) { clk_t v = value; value ++; return v; }
} clk;

const int machine_clock_rate = 1023000;

bool read_blob(char *name, unsigned char *b, size_t sz)
{
    FILE *fp = fopen(name, "rb");
    if(fp == NULL) {
        fprintf(stderr, "failed to open %s for reading\n", name);
        fclose(fp);
        return false;
    }
    size_t length = fread(b, 1, sz, fp);
    if(length < sz) {
        fprintf(stderr, "File read from %s was unexpectedly short (%zd bytes, expected %zd)\n", name, length, sz);
        perror("read_blob");
        fclose(fp);
        return false;
    }
    fclose(fp);
    return true;
}

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

    backed_region(const char* name, int base, int size, MemoryType type_, vector<backed_region*>* regions, enabled_func enabled_) :
        region{name, base, size},
        memory(size),
        type(type_),
        read_enabled(enabled_),
        write_enabled(enabled_)
    {
        std::fill(memory.begin(), memory.end(), 0x00);
        if(regions)
            regions->push_back(this);
    }

    backed_region(const char* name, int base, int size, MemoryType type_, vector<backed_region*>* regions, enabled_func read_enabled_, enabled_func write_enabled_) :
        region{name, base, size},
        memory(size),
        type(type_),
        read_enabled(read_enabled_),
        write_enabled(write_enabled_)
    {
        std::fill(memory.begin(), memory.end(), 0x00);
        if(regions)
            regions->push_back(this);
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

unsigned char floppy_header[21] = {
	0xD5, 0xAA, 0x96, 0xFF, 0xFE, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0xDE, 0xAA, 0xFF,	0xFF, 0xFF,
	0xFF, 0xFF, 0xD5, 0xAA, 0xAD };
unsigned char floppy_doSector[16] = {
	0x0, 0x7, 0xE, 0x6, 0xD, 0x5, 0xC, 0x4, 0xB, 0x3, 0xA, 0x2, 0x9, 0x1, 0x8, 0xF };
unsigned char floppy_poSector[16] = {
	0x0, 0x8, 0x1, 0x9, 0x2, 0xA, 0x3, 0xB, 0x4, 0xC, 0x5, 0xD, 0x6, 0xE, 0x7, 0xF };

void floppy_NybblizeImage(unsigned char *image, unsigned char *nybblized)
{
	// Format of a sector is header (23) + nybbles (343) + footer (30) = 396
	// (short by 20 bytes of 416 [413 if 48 byte header is one time only])
	// hdr (21) + nybbles (343) + footer (48) = 412 bytes per sector
	// (not incl. 64 byte track marker)

	static unsigned char footer[48] = {
		0xDE, 0xAA, 0xEB, 0xFF, 0xEB, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

	static unsigned char diskbyte[0x40] = {
		0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6,
		0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
		0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC,
		0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
		0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE,
		0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
		0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
		0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF };

	memset(nybblized, 0xFF, 232960);					// Doesn't matter if 00s or FFs...

        unsigned char *p = nybblized;

	for(unsigned char trk=0; trk<35; trk++)
	{
		memset(p, 0xFF, 64);					// Write gap 1, 64 bytes (self-sync)
		p += 64;

		for(unsigned char sector=0; sector<16; sector++)
		{
			memcpy(p, floppy_header, 21);			// Set up the sector header

			p[5] = ((trk >> 1) & 0x55) | 0xAA;
			p[6] =  (trk       & 0x55) | 0xAA;
			p[7] = ((sector >> 1) & 0x55) | 0xAA;
			p[8] =  (sector       & 0x55) | 0xAA;
			p[9] = (((trk ^ sector ^ 0xFE) >> 1) & 0x55) | 0xAA;
			p[10] = ((trk ^ sector ^ 0xFE)       & 0x55) | 0xAA;

			p += 21;
			unsigned char * bytes = image;

			// if (diskType[driveNum] == DT_DOS33)
				bytes += (floppy_doSector[sector] * 256) + (trk * 256 * 16);
			// else if (diskType[driveNum] == DT_PRODOS)
				// bytes += (poSector[sector] * 256) + (trk * 256 * 16);
			// else
				// bytes += (sector * 256) + (trk * 256 * 16);

			// Convert the 256 8-bit bytes into 342 6-bit bytes.

			for(int i=0; i<0x56; i++)
			{
				p[i] = ((bytes[(i + 0xAC) & 0xFF] & 0x01) << 7)
					| ((bytes[(i + 0xAC) & 0xFF] & 0x02) << 5)
					| ((bytes[(i + 0x56) & 0xFF] & 0x01) << 5)
					| ((bytes[(i + 0x56) & 0xFF] & 0x02) << 3)
					| ((bytes[(i + 0x00) & 0xFF] & 0x01) << 3)
					| ((bytes[(i + 0x00) & 0xFF] & 0x02) << 1);
			}

			p[0x54] &= 0x3F;
			p[0x55] &= 0x3F;
			memcpy(p + 0x56, bytes, 256);

			// XOR the data block with itself, offset by one byte,
			// creating a 343rd byte which is used as a cheksum.

			p[342] = 0x00;

			for(int i=342; i>0; i--)
				p[i] = p[i] ^ p[i - 1];

			// Using a lookup table, convert the 6-bit bytes into disk bytes.

			for(int i=0; i<343; i++)
                            p[i] = diskbyte[p[i] >> 2];
			p += 343;

			// Done with the nybblization, now for the epilogue...

			memcpy(p, footer, 48);
			p += 48;
		}
	}
}


struct DISKIIboard : board_base
{
    const unsigned int CA0 = 0xC0E0; // stepper phase 0 / control line 0
    const unsigned int CA1 = 0xC0E2; // stepper phase 1 / control line 1
    const unsigned int CA2 = 0xC0E4; // stepper phase 2 / control line 2
    const unsigned int CA3 = 0xC0E6; // stepper phase 3 / control strobe
    const unsigned int ENABLE = 0xC0E8; // disk drive off/on
    const unsigned int SELECT = 0xC0EA; // select drive 1/2
    const unsigned int Q6L = 0xC0EC; // IO strobe for read
    const unsigned int Q6H = 0xC0ED; // IO strobe for write
    const unsigned int Q7L = 0xC0EE; // IO strobe for clear
    const unsigned int Q7H = 0xC0EF; // IO strobe for shift

    map<unsigned int, string> io = {
        {0xC0E0, "CA0OFF"},
        {0xC0E1, "CA0ON"},
        {0xC0E2, "CA1OFF"},
        {0xC0E3, "CA1ON"},
        {0xC0E4, "CA2OFF"},
        {0xC0E5, "CA2ON"},
        {0xC0E6, "CA3OFF"},
        {0xC0E7, "CA3ON"},
        {0xC0E8, "DISABLE"},
        {0xC0E9, "ENABLE"},
        {0xC0EA, "SELECT0"},
        {0xC0EB, "SELECT1"},
        {0xC0EC, "Q6L"},
        {0xC0ED, "Q6H"},
        {0xC0EE, "Q7L"},
        {0xC0EF, "Q7H"},
    };

    backed_region rom_C600 = {"rom_C600", 0xC600, 0x0100, ROM, nullptr, [&]{return true;}};

    unsigned char floppy_image[2][143360];
    unsigned char floppy_nybblized[2][232960];
    const unsigned int bytes_per_nybblized_track = 6656;
    bool floppy_present[2];

    int drive_selected = 0;
    bool drive_motor_enabled[2];
    enum {READ, WRITE} head_mode = READ;
    unsigned char data_latch = 0x00;
    int head_stepper_phase[4] = {0, 0, 0, 0};
    int head_stepper_most_recent_phase = 0;
    int track_number = 0; // physical track number - DOS and ProDOS only use even tracks
    unsigned int track_byte = 0;

    void set_floppy(int number, char *name) // number 0 or 1; name = NULL to eject
    {
        floppy_present[number] = false;
        if(name) {
            if(!read_blob(name, floppy_image[number], sizeof(floppy_image[0])))
                throw "Couldn't read floppy";
            
            floppy_present[number] = true;
            floppy_NybblizeImage(floppy_image[number], floppy_nybblized[number]);
        }
    }

    DISKIIboard(unsigned char diskII_rom[256], char *floppy0_name, char *floppy1_name)
    {
        std::copy(diskII_rom, diskII_rom + 0x100, rom_C600.memory.begin());
        set_floppy(0, floppy0_name);
        set_floppy(1, floppy1_name);
    }

    unsigned char read_next_nybblized_byte()
    {
        if(head_mode != READ || !drive_motor_enabled[drive_selected] || !floppy_present[drive_selected])
            return 0x00;
        int i = track_byte;
        track_byte = (track_byte + 1) % bytes_per_nybblized_track;
        return floppy_nybblized[drive_selected][(track_number / 2) * bytes_per_nybblized_track + i];
    }

    void control_track_motor(unsigned int addr)
    {
        int phase = (addr & 0x7) >> 1;
        int state = addr & 0x1;
        head_stepper_phase[phase] = state;
        if(debug & DEBUG_FLOPPY) printf("stepper %04X, phase %d, state %d, so stepper motor state now: %d, %d, %d, %d\n",
            addr, phase, state,
            head_stepper_phase[0], head_stepper_phase[1],
            head_stepper_phase[2], head_stepper_phase[3]);
        if(state == 1) { // turn stepper motor phase on
            if(head_stepper_most_recent_phase == (((phase - 1) + 4) % 4)) { // stepping up
                track_number = min(track_number + 1, 70);
                if(debug & DEBUG_FLOPPY) printf("track number now %d\n", track_number);
            } else if(head_stepper_most_recent_phase == ((phase + 1) % 4)) { // stepping down
                track_number = max(0, track_number - 1);
                if(debug & DEBUG_FLOPPY) printf("track number now %d\n", track_number);
            } else if(head_stepper_most_recent_phase == phase) { // unexpected condition
                if(debug & DEBUG_FLOPPY) printf("track head stepper no change\n");
            } else { // unexpected condition
                if(debug & DEBUG_WARN) fprintf(stderr, "unexpected track stepper motor state: %d, %d, %d, %d\n",
                    head_stepper_phase[0], head_stepper_phase[1],
                    head_stepper_phase[2], head_stepper_phase[3]);
                if(debug & DEBUG_WARN) fprintf(stderr, "most recent phase: %d\n", head_stepper_most_recent_phase);
            }
            head_stepper_most_recent_phase = phase;
        }
    }

    virtual bool write(int addr, unsigned char data)
    {
        if(addr < 0xC0E0 || addr > 0xC0EF)
            return false;
        if(debug & DEBUG_RW) printf("DISK II unhandled write of %02X to %04X (%s)\n", data, addr, io[addr].c_str());
        return false;
    }
    virtual bool read(int addr, unsigned char &data)
    {
        if(rom_C600.read(addr, data)) {
            if(debug & DEBUG_RW) printf("DiskII read 0x%04X -> %02X\n", addr, data);
            return true;
        }

        if(addr < 0xC0E0 || addr > 0xC0EF) {
            return false;
        }

        if(addr >= CA0 && addr <= (CA3 + 1)) {
            if(debug & DEBUG_FLOPPY) printf("floppy control track motor\n");
            control_track_motor(addr);
            data = 0;
            return true;
        } else if(addr == Q6L) { // 0xC0EC
            data = read_next_nybblized_byte();
            if(debug & DEBUG_FLOPPY) printf("floppy read byte : %02X\n", data);
            return true;
        } else if(addr == Q6H) { // 0xC0ED
            if(debug & DEBUG_FLOPPY) printf("floppy read latch\n");
            data = data_latch; // XXX do something with the latch - e.g. set write-protect bit
            data = 0;
            return true;
        } else if(addr == Q7L) { // 0xC0EE
            if(debug & DEBUG_FLOPPY) printf("floppy set read\n");
            head_mode = READ;
            data = 0;
            return true;
        } else if(addr == Q7H) { // 0xC0EF
            if(debug & DEBUG_FLOPPY) printf("floppy set write\n");
            head_mode = WRITE;
            data = 0;
            return true;
        } else if(addr == SELECT) {
            if(debug & DEBUG_FLOPPY) printf("floppy select first drive\n");
            drive_selected = 0;
            return true;
        } else if(addr == SELECT + 1) {
            if(debug & DEBUG_FLOPPY) printf("floppy select second drive\n");
            drive_selected = 1;
            return true;
        } else if(addr == ENABLE) {
            if(debug & DEBUG_FLOPPY) printf("floppy switch off\n");
            drive_motor_enabled[drive_selected] = false;
            // go disable reading
            // disable other drive?
            return true;
        } else if(addr == ENABLE + 1) {
            if(debug & DEBUG_FLOPPY) printf("floppy switch on\n");
            drive_motor_enabled[drive_selected] = true;
            // go enable reading
            // disable other drive?
            return true;
        }
        printf("DISK II unhandled read from %04X (%s)\n", addr, io[addr].c_str());
        data = 0;
        return true;
    }
    virtual void reset(void) {}
};

struct MAINboard : board_base
{
    system_clock& clk;

    vector<board_base*> boards;

    vector<SoftSwitch*> switches;
    SoftSwitch* switches_by_address[256];
    SoftSwitch CXROM {"CXROM", 0xC006, 0xC007, 0xC015, false, switches, true};
    SoftSwitch STORE80 {"STORE80", 0xC000, 0xC001, 0xC018, false, switches, true};
    SoftSwitch RAMRD {"RAMRD", 0xC002, 0xC003, 0xC013, false, switches, true};
    SoftSwitch RAMWRT {"RAMWRT", 0xC004, 0xC005, 0xC014, false, switches, true};
    SoftSwitch ALTZP {"ALTZP", 0xC008, 0xC009, 0xC016, false, switches, true};
    SoftSwitch C3ROM {"C3ROM", 0xC00A, 0xC00B, 0xC017, false, switches, true};
    SoftSwitch ALTCHAR {"ALTCHAR", 0xC00E, 0xC00F, 0xC01E, false, switches};
    SoftSwitch VID80 {"VID80", 0xC00C, 0xC00D, 0xC01F, false, switches};
    SoftSwitch TEXT {"TEXT", 0xC050, 0xC051, 0xC01A, true, switches, true};
    SoftSwitch MIXED {"MIXED", 0xC052, 0xC053, 0xC01B, true, switches, true};
    SoftSwitch PAGE2 {"PAGE2", 0xC054, 0xC055, 0xC01C, true, switches, true};
    SoftSwitch HIRES {"HIRES", 0xC056, 0xC057, 0xC01D, true, switches, true};

    vector<backed_region*> regions;
    vector<backed_region*> regions_by_page[256];

    backed_region szp = {"szp", 0x0000, 0x0200, RAM, &regions, [&](){return !ALTZP;}}; // stack and zero page
    backed_region aszp = {"aszp", 0x0000, 0x0200, RAM, &regions, [&](){return ALTZP;}}; // alternate stack and zero page

    bool internal_C800_ROM_selected;
    backed_region rom_C100 = {"rom_C100", 0xC100, 0x0200, ROM, &regions, [&]{return CXROM;}};
    backed_region rom_C300 = {"rom_C300", 0xC300, 0x0100, ROM, &regions, [&]{return CXROM || (!CXROM && !C3ROM);}};
    backed_region rom_C400 = {"rom_C400", 0xC300, 0x0400, ROM, &regions, [&]{return CXROM;}};
    backed_region rom_C800 = {"rom_C800", 0xC800, 0x0800, ROM, &regions, [&]{return CXROM || (!CXROM && !C3ROM && internal_C800_ROM_selected);}};
    backed_region rom_CXXX_default = {"rom_CXXX_default", 0xC100, 0x0F00, ROM, &regions, [&]{return true;}};

    enabled_func read_from_aux_ram = [&]{return RAMRD;};
    enabled_func write_to_aux_ram = [&]{return RAMWRT;};
    enabled_func read_from_main_ram = [&]{return !read_from_aux_ram();};
    enabled_func write_to_main_ram = [&]{return !write_to_aux_ram();};

    backed_region ram_0200 = {"ram_0200", 0x0200, 0x0200, RAM, &regions, read_from_main_ram, write_to_main_ram};
    backed_region ram_0200_x = {"ram_0200_x", 0x0200, 0x0200, RAM, &regions, read_from_aux_ram, write_to_aux_ram};
    backed_region ram_0C00 = {"ram_0C00", 0x0C00, 0x1400, RAM, &regions, read_from_main_ram, write_to_main_ram};
    backed_region ram_0C00_x = {"ram_0C00_x", 0x0C00, 0x1400, RAM, &regions, read_from_aux_ram, write_to_aux_ram};
    backed_region ram_6000 = {"ram_6000", 0x6000, 0x6000, RAM, &regions, read_from_main_ram, write_to_main_ram};
    backed_region ram_6000_x = {"ram_6000_x", 0x6000, 0x6000, RAM, &regions, read_from_aux_ram, write_to_aux_ram};

    enabled_func read_from_aux_text1 = [&]{return RAMRD && ((!STORE80) || (STORE80 && PAGE2));};
    enabled_func write_to_aux_text1 = [&]{return RAMWRT && ((!STORE80) || (STORE80 && PAGE2));};
    enabled_func read_from_main_text1 = [&]{return !read_from_aux_text1();};
    enabled_func write_to_main_text1 = [&]{return !write_to_aux_text1();};

    backed_region text_page1 = {"text_page1", 0x0400, 0x0400, RAM, &regions, read_from_main_text1, write_to_main_text1};
    backed_region text_page1x = {"text_page1x", 0x0400, 0x0400, RAM, &regions, read_from_aux_text1, write_to_aux_text1};
    backed_region text_page2 = {"text_page2", 0x0800, 0x0400, RAM, &regions, read_from_main_ram, write_to_main_ram};
    backed_region text_page2x = {"text_page2x", 0x0800, 0x0400, RAM, &regions, read_from_aux_ram, write_to_aux_ram};

    enabled_func read_from_aux_hires1 = [&]{return HIRES && RAMRD && ((!STORE80) || (STORE80 && PAGE2));};
    enabled_func write_to_aux_hires1 = [&]{return HIRES && RAMWRT && ((!STORE80) || (STORE80 && PAGE2));};
    enabled_func read_from_main_hires1 = [&]{return !read_from_aux_hires1();};
    enabled_func write_to_main_hires1 = [&]{return !write_to_aux_hires1();};

    backed_region hires_page1 = {"hires_page1", 0x2000, 0x2000, RAM, &regions, read_from_main_hires1, write_to_main_hires1};
    backed_region hires_page1x = {"hires_page1x", 0x2000, 0x2000, RAM, &regions, read_from_aux_hires1, write_to_aux_hires1};
    backed_region hires_page2 = {"hires_page2", 0x4000, 0x2000, RAM, &regions, read_from_main_ram, write_to_main_ram};
    backed_region hires_page2x = {"hires_page2x", 0x4000, 0x2000, RAM, &regions, read_from_aux_ram, write_to_aux_ram};

    enum {BANK1, BANK2} C08X_bank;
    bool C08X_read_RAM;
    bool C08X_write_RAM;

    backed_region rom_D000 = {"rom_D000", 0xD000, 0x1000, ROM, &regions, [&]{return !C08X_read_RAM;}};
    backed_region rom_E000 = {"rom_E000", 0xE000, 0x2000, ROM, &regions, [&]{return !C08X_read_RAM;}};

    backed_region ram1_main_D000 = {"ram1_main_D000", 0xD000, 0x1000, RAM, &regions, [&]{return !ALTZP && C08X_read_RAM && (C08X_bank == BANK1);}, [&]{return !ALTZP && C08X_write_RAM && (C08X_bank == BANK1);}};
    backed_region ram2_main_D000 = {"ram2_main_D000", 0xD000, 0x1000, RAM, &regions, [&]{return !ALTZP && C08X_read_RAM && (C08X_bank == BANK2);}, [&]{return !ALTZP && C08X_write_RAM && (C08X_bank == BANK2);}};
    backed_region ram_main_E000 = {"ram1_main_E000", 0xE000, 0x2000, RAM, &regions, [&]{return C08X_read_RAM;}, [&]{return !ALTZP && C08X_write_RAM;}};
    backed_region ram1_main_D000_x = {"ram1_main_D000_x", 0xD000, 0x1000, RAM, &regions, [&]{return ALTZP && C08X_read_RAM && (C08X_bank == BANK1);}, [&]{return ALTZP && C08X_write_RAM && (C08X_bank == BANK1);}};
    backed_region ram2_main_D000_x = {"ram2_main_D000_x", 0xD000, 0x1000, RAM, &regions, [&]{return ALTZP && C08X_read_RAM && (C08X_bank == BANK2);}, [&]{return ALTZP && C08X_write_RAM && (C08X_bank == BANK2);}};
    backed_region ram_main_E000_x = {"ram1_main_E000_x", 0xE000, 0x2000, RAM, &regions, [&]{return ALTZP && C08X_read_RAM;}, [&]{return ALTZP && C08X_write_RAM;}};

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

    static const int sample_rate = 44100;
    static const size_t audio_buffer_size = sample_rate / 10;
    char audio_buffer[audio_buffer_size];
    long long audio_buffer_start_sample = 0;
    long long audio_buffer_next_sample = -1;
    bool speaker_energized = false;

    void fill_flush_audio()
    {
        long long current_sample = clk * sample_rate / machine_clock_rate;
        for(long long i = audio_buffer_next_sample; i < current_sample; i++) {
            audio_buffer[i % audio_buffer_size] = speaker_energized ? 128 - 32 : 128 + 32;
            if(i - audio_buffer_start_sample == audio_buffer_size - 1) {
                audio_flush(audio_buffer, audio_buffer_size);

                audio_buffer_start_sample = i + 1;
            }
        }
        audio_buffer_next_sample = current_sample;
    }

    // flush anything needing flushing
    void sync()
    {
        fill_flush_audio();
    }

    void enqueue_key(unsigned char k)
    {
        keyboard_buffer.push_back(k);
    }

    typedef std::function<bool (int addr, unsigned char data)> display_write_func;
    display_write_func display_write;
    typedef std::function<void (char *audiobuffer, size_t dist)> audio_flush_func;
    audio_flush_func audio_flush;
    MAINboard(system_clock& clk_, unsigned char rom_image[32768],  display_write_func display_write_, audio_flush_func audio_flush_) :
        clk(clk_),
        internal_C800_ROM_selected(true),
        display_write(display_write_),
        audio_flush(audio_flush_)
    {
        std::copy(rom_image + rom_D000.base - 0x8000, rom_image + rom_D000.base - 0x8000 + rom_D000.size, rom_D000.memory.begin());
        std::copy(rom_image + rom_E000.base - 0x8000, rom_image + rom_E000.base - 0x8000 + rom_E000.size, rom_E000.memory.begin());
        std::copy(rom_image + rom_C100.base - 0x8000, rom_image + rom_C100.base - 0x8000 + rom_C100.size, rom_C100.memory.begin());
        std::copy(rom_image + rom_C300.base - 0x8000, rom_image + rom_C300.base - 0x8000 + rom_C300.size, rom_C300.memory.begin());
        std::copy(rom_image + rom_C400.base - 0x8000, rom_image + rom_C400.base - 0x8000 + rom_C400.size, rom_C400.memory.begin());
        std::copy(rom_image + rom_C800.base - 0x8000, rom_image + rom_C800.base - 0x8000 + rom_C800.size, rom_C800.memory.begin());

        for(auto it = regions.begin(); it != regions.end(); it++) {
            backed_region* r = *it;
            int firstpage = r->base / 256;
            int lastpage = (r->base + r->size + 255) / 256 - 1;
            for(int i = firstpage; i <= lastpage; i++) {
                regions_by_page[i].push_back(r);
            }
        }
        for(int i = 0; i < 256; i++)
            switches_by_address[i] = NULL;
        for(auto it = switches.begin(); it != switches.end(); it++) {
            SoftSwitch* sw = *it;
            switches_by_address[sw->clear_address - 0xC000] = sw;
            switches_by_address[sw->set_address - 0xC000] = sw;
            switches_by_address[sw->read_address - 0xC000] = sw;
        }
    }

    virtual ~MAINboard()
    {
    }

    virtual void reset()
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
        for(auto it = boards.begin(); it != boards.end(); it++) {
            board_base* b = *it;
            if(b->read(addr, data)) {
                return true;
            }
        }
        if(io_region.contains(addr)) {
            if(exit_on_banking && (banking_read_switches.find(addr) != banking_read_switches.end())) {
                printf("bank switch control %04X, aborting\n", addr);
                exit(1);
            }
            SoftSwitch* sw = switches_by_address[addr - 0xC000];
            if(sw != NULL) {
                if(addr == sw->read_address) {
                    data = sw->enabled ? 0x80 : 0x00;
                    if(debug & DEBUG_SWITCH) printf("Read status of %s = %02X\n", sw->name.c_str(), data);
                    return true;
                } else if(sw->read_also_changes && addr == sw->set_address) {
                    if(!sw->implemented) { printf("%s ; set is unimplemented\n", sw->name.c_str()); fflush(stdout); exit(0); }
                    data = 0xff;
                    sw->enabled = true;
                    if(debug & DEBUG_SWITCH) printf("Set %s\n", sw->name.c_str());
                    return true;
                } else if(sw->read_also_changes && addr == sw->clear_address) {
                    if(!sw->implemented) { printf("%s ; unimplemented\n", sw->name.c_str()); fflush(stdout); exit(0); }
                    data = 0xff;
                    sw->enabled = false;
                    if(debug & DEBUG_SWITCH) printf("Clear %s\n", sw->name.c_str());
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
                C08X_write_RAM = addr & 1;
                int read_ROM = ((addr >> 1) & 1) ^ C08X_write_RAM;
                C08X_read_RAM = !read_ROM;
                if(debug & DEBUG_SWITCH) printf("write %04X switch, %s, %d write_RAM, %d read_RAM\n", addr, (C08X_bank == BANK1) ? "BANK1" : "BANK2", C08X_write_RAM, C08X_read_RAM);
                data = 0x00;
                return true;
            }
            if(addr == 0xC011) {
                data = (C08X_bank == BANK2) ? 0x80 : 0x0;
                data = 0x00;
                if(debug & DEBUG_SWITCH) printf("read BSRBANK2, return 0x%02X\n", data);
                return true;
            }
            if(addr == 0xC012) {
                data = C08X_read_RAM ? 0x80 : 0x0;
                if(debug & DEBUG_SWITCH) printf("read BSRREADRAM, return 0x%02X\n", data);
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

                fill_flush_audio();
                data = 0x00;
                speaker_energized = !speaker_energized;
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
        for(auto it = regions_by_page[addr / 256].begin(); it != regions_by_page[addr / 256].end(); it++) {
            backed_region* r = *it;
            if(r->read(addr, data)) {
                if(debug & DEBUG_RW) printf("read 0x%04X -> 0x%02X from %s\n", addr, data, r->name.c_str());
                return true;
            }
        }
        if((addr & 0xFF00) == 0xC300) {
            if(debug & DEBUG_SWITCH) printf("read 0x%04X, enabling internal C800 ROM\n", addr);
            internal_C800_ROM_selected = true;
        }
        if(addr == 0xCFFF) {
            if(debug & DEBUG_SWITCH) printf("read 0xCFFF, disabling internal C800 ROM\n");
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
#if 0
        if(text_page1.write(addr, data) ||
            text_page1x.write(addr, data) ||
            text_page2.write(addr, data) ||
            text_page2x.write(addr, data) ||
            hires_page1.write(addr, data) ||
            hires_page1x.write(addr, data) ||
            hires_page2.write(addr, data) ||
            hires_page2x.write(addr, data))
#else
        if(((addr >= 0x400) && (addr <= 0xBFF)) || ((addr >= 0x2000) && (addr <= 0x5FFF)))
#endif
        {
            display_write(addr, data);
        }
        for(auto it = boards.begin(); it != boards.end(); it++) {
            board_base* b = *it;
            if(b->write(addr, data)) {
                return true;
            }
        }
        if(io_region.contains(addr)) {
            if(exit_on_banking && (banking_write_switches.find(addr) != banking_write_switches.end())) {
                printf("bank switch control %04X, exiting\n", addr);
                exit(1);
            }
            SoftSwitch* sw = switches_by_address[addr - 0xC000];
            if(sw != NULL) {
                if(addr == sw->set_address) {
                    if(!sw->implemented) { printf("%s ; set is unimplemented\n", sw->name.c_str()); fflush(stdout); exit(0); }
                    data = 0xff;
                    sw->enabled = true;
                    if(debug & DEBUG_SWITCH) printf("Set %s\n", sw->name.c_str());
                    return true;
                } else if(addr == sw->clear_address) {
                    // if(!sw->implemented) { printf("%s ; unimplemented\n", sw->name.c_str()); fflush(stdout); exit(0); }
                    data = 0xff;
                    sw->enabled = false;
                    if(debug & DEBUG_SWITCH) printf("Clear %s\n", sw->name.c_str());
                    return true;
                }
            }
            if((addr & 0xFFF0) == 0xC080) {
                C08X_bank = ((addr >> 3) & 1) ? BANK1 : BANK2;
                C08X_write_RAM = addr & 1;
                int read_ROM = ((addr >> 1) & 1) ^ C08X_write_RAM;
                C08X_read_RAM = !read_ROM;
                if(debug & DEBUG_SWITCH) printf("write %04X switch, %s, %d write_RAM, %d read_RAM\n", addr, (C08X_bank == BANK1) ? "BANK1" : "BANK2", C08X_write_RAM, C08X_read_RAM);
                data = 0x00;
                return true;
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
        for(auto it = regions_by_page[addr / 256].begin(); it != regions_by_page[addr / 256].end(); it++) {
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

struct bus_frontend
{
    board_base* board;
    map<int, vector<unsigned char> > writes;
    map<int, vector<unsigned char> > reads;

    unsigned char read(int addr)
    {
        unsigned char data = 0xaa;
        if(board->read(addr, data)) {
            if(debug & DEBUG_BUS) printf("read %04X returned %02X\n", addr, data);
            // reads[addr].push_back(data);
            return data;
        }
        if(debug & DEBUG_ERROR)
            fprintf(stderr, "no ownership of read at %04X\n", addr);
        return 0xAA;
    }
    void write(int addr, unsigned char data)
    {
        if(board->write(addr, data)) {
            if(debug & DEBUG_BUS) printf("write %04X %02X\n", addr, data);
            // writes[addr].push_back(data);
            return;
        }
        if(debug & DEBUG_ERROR)
            fprintf(stderr, "no ownership of write %02X at %04X\n", data, addr);
    }

    void reset()
    {
        board->reset();
    }
};

bus_frontend bus;

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
    system_clock &clk;

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
    CPU6502(system_clock& clk_) :
        clk(clk_),
        a(0),
        x(0),
        y(0),
        s(0),
        p(0x20),
        exception(RESET)
    {
    }
    void stack_push(bus_frontend& bus, unsigned char d)
    {
        bus.write(0x100 + s--, d);
    }
    unsigned char stack_pull(bus_frontend& bus)
    {
        return bus.read(0x100 + ++s);
    }
    unsigned char read_pc_inc(bus_frontend& bus)
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
    void reset(bus_frontend& bus)
    {
        s = 0xFD;
        pc = bus.read(0xFFFC) + bus.read(0xFFFD) * 256;
        exception = NONE;
    }
    void irq(bus_frontend& bus)
    {
        stack_push(bus, (pc + 0) >> 8);
        stack_push(bus, (pc + 0) & 0xFF);
        stack_push(bus, p);
        pc = bus.read(0xFFFE) + bus.read(0xFFFF) * 256;
        exception = NONE;
    }
    void brk(bus_frontend& bus)
    {
        stack_push(bus, (pc - 1) >> 8);
        stack_push(bus, (pc - 1) & 0xFF);
        stack_push(bus, p | B); // | B says the Synertek 6502 reference
        pc = bus.read(0xFFFE) + bus.read(0xFFFF) * 256;
        exception = NONE;
    }
    void nmi(bus_frontend& bus)
    {
        stack_push(bus, (pc + 0) >> 8);
        stack_push(bus, (pc + 0) & 0xFF);
        stack_push(bus, p);
        pc = bus.read(0xFFFA) + bus.read(0xFFFB) * 256;
        exception = NONE;
    }
    int cycles[256] = 
    {
        7, 6, -1, -1, -1, 3, 5, -1, 3, 2, 2, -1, -1, 4, 6, -1,
        2, 5, -1, -1, -1, 4, 6, -1, 2, 4, -1, -1, -1, 4, 7, -1,
        6, 6, -1, -1, 3, 3, 5, -1, 4, 2, 2, -1, 4, 4, 6, -1,
        2, 5, -1, -1, -1, 4, 6, -1, 2, 4, -1, -1, -1, 4, 7, -1,
        6, 6, -1, -1, -1, 3, 5, -1, 3, 2, 2, -1, 3, 4, 6, -1,
        2, 5, -1, -1, -1, 4, 6, -1, 2, 4, -1, -1, -1, 4, 7, -1,
        6, 6, -1, -1, -1, 3, 5, -1, 4, 2, 2, -1, 5, 4, 6, -1,
        2, 5, -1, -1, -1, 4, 6, -1, 2, 4, -1, -1, -1, 4, 7, -1,
        -1, 6, -1, -1, 3, 3, 3, -1, 2, -1, 2, -1, 4, 4, 4, -1,
        2, 6, -1, -1, 4, 4, 4, -1, 2, 5, 2, -1, -1, 5, -1, -1,
        2, 6, 2, -1, 3, 3, 3, -1, 2, 2, 2, -1, 4, 4, 4, -1,
        2, 5, -1, -1, 4, 4, 4, -1, 2, 4, 2, -1, 4, 4, 4, -1,
        2, 6, -1, -1, 3, 3, 5, -1, 2, 2, 2, -1, 4, 4, 3, -1,
        2, 5, -1, -1, -1, 4, 6, -1, 2, 4, -1, -1, -1, 4, 7, -1,
        2, 6, -1, -1, 3, 3, 5, -1, 2, 2, 2, -1, 4, 4, 6, -1,
        2, 5, -1, -1, -1, 4, 6, -1, 2, 4, -1, -1, -1, 4, 7, -1,
    };
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
    int get_operand(bus_frontend& bus, Operand oper)
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
    void cycle(bus_frontend& bus)
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

            case 0xEE: { // INC abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, m = bus.read(addr) + 1);
                bus.write(addr, m);
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
                if(!isset(N)) {
                    clk++;
                    if((pc + rel) / 256 != pc / 256)
                        clk++;
                    pc += rel;
                }
                break;
            }

            case 0x50: { // BVC
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(!isset(V)) {
                    clk++;
                    if((pc + rel) / 256 != pc / 256)
                        clk++;
                    pc += rel;
                }
                break;
            }

            case 0x70: { // BVS
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(isset(V)) {
                    clk++;
                    if((pc + rel) / 256 != pc / 256)
                        clk++;
                    pc += rel;
                }
                break;
            }

            case 0x30: { // BMI
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(isset(N)) {
                    clk++;
                    if((pc + rel) / 256 != pc / 256)
                        clk++;
                    pc += rel;
                }
                break;
            }

            case 0x90: { // BCC
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(!isset(C)) {
                    clk++;
                    if((pc + rel) / 256 != pc / 256)
                        clk++;
                    pc += rel;
                }
                break;
            }

            case 0xB0: { // BCS
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(isset(C)) {
                    clk++;
                    if((pc + rel) / 256 != pc / 256)
                        clk++;
                    pc += rel;
                }
                break;
            }

            case 0xD0: { // BNE
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(!isset(Z)) {
                    clk++;
                    if((pc + rel) / 256 != pc / 256)
                        clk++;
                    pc += rel;
                }
                break;
            }

            case 0xF0: { // BEQ
                int rel = (read_pc_inc(bus) + 128) % 256 - 128;
                if(isset(Z)) {
                    clk++;
                    if((pc + rel) / 256 != pc / 256)
                        clk++;
                    pc += rel;
                }
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
                if((addr - y) / 256 != addr / 256)
                    clk++;
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
                if((addr + x) / 256 != addr / 256)
                    clk++;
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xD9: { // CMP abs, Y
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr + y);
                if((addr + y) / 256 != addr / 256)
                    clk++;
                flag_change(C, m <= a);
                set_flags(N | Z, m = a - m);
                break;
            }

            case 0xB9: { // LDA abs, Y
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, a = bus.read(addr + y));
                if((addr + y) / 256 != addr / 256)
                    clk++;
                break;
            }

            case 0xBC: { // LDY abs, X
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, y = bus.read(addr + x));
                if((addr + x) / 256 != addr / 256)
                    clk++;
                break;
            }

            case 0xBD: { // LDA abs, X
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, a = bus.read(addr + x));
                if((addr + x) / 256 != addr / 256)
                    clk++;
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
                if((addr - y) / 256 != addr / 256)
                    clk++;
                m = bus.read(addr);
                int borrow = isset(C) ? 0 : 1;
                flag_change(C, !(a < (m + borrow)));
                flag_change(V, sbc_overflow(a, m, borrow));
                set_flags(N | Z, a = a - (m + borrow));
                break;
            }

            case 0xF9: { // SBC abs, Y
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk++;
                unsigned char m = bus.read(addr);
                int borrow = isset(C) ? 0 : 1;
                flag_change(C, !(a < (m + borrow)));
                flag_change(V, sbc_overflow(a, m, borrow));
                set_flags(N | Z, a = a - (m + borrow));
                break;
            }

            case 0xFD: { // SBC abs, X
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256 + x;
                if((addr - x) / 256 != addr / 256)
                    clk++;
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

            case 0x71: { // ADC (ind), Y
                unsigned char zpg = read_pc_inc(bus);
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk++;
                m = bus.read(addr);
                int carry = isset(C) ? 1 : 0;
                flag_change(C, (int)(a + m + carry) > 0xFF);
                flag_change(V, adc_overflow(a, m, carry));
                set_flags(N | Z, a = a + m + carry);
                break;
            }

            case 0x6D: { // ADC abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr);
                int carry = isset(C) ? 1 : 0;
                flag_change(C, (int)(a + m + carry) > 0xFF);
                flag_change(V, adc_overflow(a, m, carry));
                set_flags(N | Z, a = a + m + carry);
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

            case 0x7D: { // ADC abs, X
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256 + x;
                if((addr - x) / 256 != addr / 256)
                    clk++;
                m = bus.read(addr);
                int carry = isset(C) ? 1 : 0;
                flag_change(C, (int)(a + m + carry) > 0xFF);
                flag_change(V, adc_overflow(a, m, carry));
                set_flags(N | Z, a = a + m + carry);
                break;
            }

            case 0x79: { // ADC abs, Y
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk++;
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

            case 0x0E: { // ASL abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr);
                flag_change(C, m & 0x80);
                set_flags(N | Z, m = m << 1);
                bus.write(addr, m);
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

            case 0x5E: { // LSR abs, X
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr + x);
                flag_change(C, m & 0x01);
                set_flags(N | Z, m = m >> 1);
                bus.write(addr + x, m);
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

            case 0x15: { // ORA zpg, X
                int zpg = (read_pc_inc(bus) + x) & 0xFF;
                m = bus.read(zpg);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x0D: { // ORA abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x1D: { // ORA abs, X
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr + x);
                if((addr + x) / 256 != addr / 256)
                    clk++;
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x11: { // ORA (ind), Y
                unsigned char zpg = read_pc_inc(bus);
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk++;
                m = bus.read(addr);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x05: { // ORA zpg
                unsigned char zpg = read_pc_inc(bus);
                m = bus.read(zpg);
                set_flags(N | Z, a = a | m);
                break;
            }

            case 0x09: { // ORA imm
                unsigned char imm = read_pc_inc(bus);
                set_flags(N | Z, a = a | imm);
                break;
            }

            case 0x39: { // AND abs, y
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, a = a & bus.read(addr + y));
                if((addr + y) / 256 != addr / 256)
                    clk++;
                break;
            }

            case 0x2D: { // AND abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, a = a & bus.read(addr));
                break;
            }

            case 0x25: { // AND zpg
                unsigned char zpg = read_pc_inc(bus);
                set_flags(N | Z, a = a & bus.read(zpg));
                break;
            }

            case 0x29: { // AND imm
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

            case 0x4C: { // JMP
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
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

            case 0x9D: { // STA abs, x
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

            case 0x81: { // STA (ind, X)
                unsigned char zpg = (read_pc_inc(bus) + x) & 0xFF;
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
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

            case 0xAE: { // LDX abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                set_flags(N | Z, x = bus.read(addr));
                break;
            }

            case 0xBE: { // LDX
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk++;
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

            case 0xCC: { // CPY abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr);
                flag_change(C, m <= y);
                set_flags(N | Z, m = y - m);
                break;
            }

            case 0xEC: { // CPX abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr);
                flag_change(C, m <= x);
                set_flags(N | Z, m = x - m);
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

            case 0x41: { // EOR (ind, X)
                unsigned char zpg = (read_pc_inc(bus) + x) & 0xFF;
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x4D: { // EOR abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x5D: { // EOR abs, X
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr + x);
                if((addr + x) / 256 != addr / 256)
                    clk++;
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0x59: { // EOR abs, Y
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                m = bus.read(addr + y);
                if((addr + y) / 256 != addr / 256)
                    clk++;
                set_flags(N | Z, a = a ^ m);
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
                if((addr - y) / 256 != addr / 256)
                    clk++;
                m = bus.read(addr);
                set_flags(N | Z, a = a ^ m);
                break;
            }

            case 0xD1: { // CMP
                unsigned char zpg = read_pc_inc(bus);
                int addr = bus.read(zpg) + bus.read((zpg + 1) & 0xFF) * 256 + y;
                if((addr - y) / 256 != addr / 256)
                    clk++;
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

            case 0x8E: { // STX abs
                int addr = read_pc_inc(bus) + read_pc_inc(bus) * 256;
                bus.write(addr, x);
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
        clk += cycles[inst];
    }
};

void usage(char *progname)
{
    printf("\n");
    printf("usage: %s [-debugger] [-fast] ROM.bin\n", progname);
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

string read_bus_and_disassemble(bus_frontend &bus, int pc)
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

enum APPLE2Einterface::EventType process_events(MAINboard *board, bus_frontend& bus, CPU6502& cpu)
{
    static bool shift_down = false;
    static bool control_down = false;
    // skip CAPS for now

    while(APPLE2Einterface::event_waiting()) {
        APPLE2Einterface::event e = APPLE2Einterface::dequeue_event();
        if(e.type == APPLE2Einterface::PASTE) {
            for(int i = 0; i < strlen(e.str); i++)
                if(e.str[i] == '\n')
                    board->enqueue_key('\r');
                else
                    board->enqueue_key(e.str[i]);
            free(e.str);
        } else if(e.type == APPLE2Einterface::KEYDOWN) {
            if((e.value == APPLE2Einterface::LEFT_SHIFT) || (e.value == APPLE2Einterface::RIGHT_SHIFT))
                shift_down = true;
            else if((e.value == APPLE2Einterface::LEFT_CONTROL) || (e.value == APPLE2Einterface::RIGHT_CONTROL))
                control_down = true;
            else if(e.value == APPLE2Einterface::ENTER) {
                board->enqueue_key(141 - 128);
            } else if(e.value == APPLE2Einterface::TAB) {
                board->enqueue_key('	');
            } else if(e.value == APPLE2Einterface::ESCAPE) {
                board->enqueue_key('');
            } else if(e.value == APPLE2Einterface::DELETE) {
                board->enqueue_key(255 - 128);
            } else if(e.value == APPLE2Einterface::RIGHT) {
                board->enqueue_key(149 - 128);
            } else if(e.value == APPLE2Einterface::LEFT) {
                board->enqueue_key(136 - 128);
            } else if(e.value == APPLE2Einterface::DOWN) {
                board->enqueue_key(138 - 128);
            } else if(e.value == APPLE2Einterface::UP) {
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
        } else if(e.type == APPLE2Einterface::KEYUP) {
            if((e.value == APPLE2Einterface::LEFT_SHIFT) || (e.value == APPLE2Einterface::RIGHT_SHIFT))
                shift_down = false;
            else if((e.value == APPLE2Einterface::LEFT_CONTROL) || (e.value == APPLE2Einterface::RIGHT_CONTROL))
                control_down = false;
        } if(e.type == APPLE2Einterface::RESET) {
            bus.reset();
            cpu.reset(bus);
        } else if(e.type == APPLE2Einterface::REBOOT) {
            bus.reset();
            cpu.nmi(bus);
        } else if(e.type == APPLE2Einterface::PAUSE) {
            pause_cpu = e.value;
        } else if(e.type == APPLE2Einterface::SPEED) {
            run_fast = e.value;
        } else if(e.type == APPLE2Einterface::QUIT)
            return e.type;
    }
    return APPLE2Einterface::NONE;
}

ao_device *open_ao()
{
    ao_device *device;
    ao_sample_format format;
    int default_driver;

    ao_initialize();

    default_driver = ao_default_driver_id();

    memset(&format, 0, sizeof(format));
    format.bits = 8;
    format.channels = 1;
    format.rate = 44100;
    format.byte_format = AO_FMT_LITTLE;

    /* -- Open driver -- */
    device = ao_open_live(default_driver, &format, NULL /* no options */);
    if (device == NULL) {
        fprintf(stderr, "Error opening libao audio device.\n");
        return nullptr;
    }
    return device;
}


int main(int argc, char **argv)
{
    char *progname = argv[0];
    argc -= 1;
    argv += 1;
    char *diskII_rom_name = NULL, *floppy1_name = NULL, *floppy2_name = NULL;

    bool have_audio = true;

    while((argc > 0) && (argv[0][0] == '-')) {
	if(strcmp(argv[0], "-debugger") == 0) {
            debugging = true;
            argv++;
            argc--;
	} else if(strcmp(argv[0], "-diskII") == 0) {
            if(argc < 4) {
                fprintf(stderr, "-diskII option requires a ROM image filename and two floppy image names (or \"-\") for no floppy image.\n");
                exit(EXIT_FAILURE);
            }
            diskII_rom_name = argv[1];
            floppy1_name = argv[2];
            floppy2_name = argv[3];
            argv += 4;
            argc -= 4;
	} else if(strcmp(argv[0], "-noaudio") == 0) {
            have_audio = false;
            argv += 1;
            argc -= 1;
	} else if(strcmp(argv[0], "-fast") == 0) {
            run_fast = true;
            argv += 1;
            argc -= 1;
	} else if(strcmp(argv[0], "-d") == 0) {
            debug = atoi(argv[1]);
            if(argc < 2) {
                fprintf(stderr, "-d option requires a debugger mask value.\n");
                exit(EXIT_FAILURE);
            }
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

    if(!read_blob(romname, b, sizeof(b)))
        exit(EXIT_FAILURE);

    unsigned char diskII_rom[256];
    if(diskII_rom_name != NULL) {
        if(!read_blob(diskII_rom_name, diskII_rom, sizeof(diskII_rom)))
            exit(EXIT_FAILURE);
    }

    system_clock clk;

    ao_device *aodev = open_ao();
    if(aodev == NULL)
        exit(EXIT_FAILURE);

    MAINboard* mainboard;

    MAINboard::display_write_func display = [](int addr, unsigned char data)->bool{return APPLE2Einterface::write(addr, data);};
    MAINboard::audio_flush_func audio;
    if(have_audio)
        audio = [aodev](char *buf, size_t sz){
            static char prev_sample;
            for(int i = 0; i < sz; i++)
                if(buf[i] != prev_sample) {
                    ao_play(aodev, buf, sz);
                    break;
                }
            prev_sample = buf[sz - 1];
        };
    else
        audio = [](char *buf, size_t sz){};
    mainboard = new MAINboard(clk, b, display, audio);
    bus.board = mainboard;
    bus.reset();

    if(diskII_rom_name != NULL) {

        if((strcmp(floppy1_name, "-") == 0) || 
           (strcmp(floppy1_name, "none") == 0) || 
           (strcmp(floppy1_name, "") == 0) )
            floppy1_name = NULL;

        if((strcmp(floppy2_name, "-") == 0) || 
           (strcmp(floppy2_name, "none") == 0) || 
           (strcmp(floppy2_name, "") == 0) )
            floppy2_name = NULL;

        try {
            DISKIIboard *diskII = new DISKIIboard(diskII_rom, floppy1_name, floppy2_name);
            mainboard->boards.push_back(diskII);
        } catch(const char *msg) {
            cerr << msg << endl;
            exit(EXIT_FAILURE);
        }
    }

    CPU6502 cpu(clk);

    atexit(cleanup);

    if(!debugging) {
        start_keyboard();
    }

    if(use_fake6502)
        reset6502();

    APPLE2Einterface::start();

    while(1) {
        if(!debugging) {
            poll_keyboard();

            char key;
            bool have_key = peek_key(&key);

            if(process_events(mainboard, bus, cpu) == APPLE2Einterface::QUIT) {
                break;
            }

            if(have_key) {
                if(key == '') {
                    debugging = true;
                    printf("enter debugger\n");
                    clear_strobe();
                    stop_keyboard();
                    continue;
                } else {
                    mainboard->enqueue_key(key);
                    clear_strobe();
                }
            }

            chrono::time_point<chrono::system_clock> then;
            int clocks_per_slice;
            if(pause_cpu)
                clocks_per_slice = 0;
            else {
                if(run_fast)
                    clocks_per_slice = machine_clock_rate; 
                else
                    clocks_per_slice = millis_per_slice * machine_clock_rate / 1000;
            }
            clk_t prev_clock = clk;
            while(clk - prev_clock < clocks_per_slice) {
                if(debug & DEBUG_DECODE) {
                    string dis = read_bus_and_disassemble(bus, cpu.pc);
                    printf("%s\n", dis.c_str());
                }
                if(use_fake6502) {
                    clockticks6502 = 0;
                    step6502();
                    clk += clockticks6502;
                } else {
                    cpu.cycle(bus);
                }
            }
            mainboard->sync();
            APPLE2Einterface::DisplayMode mode = mainboard->TEXT ? APPLE2Einterface::TEXT : (mainboard->HIRES ? APPLE2Einterface::HIRES : APPLE2Einterface::LORES);
            int page = mainboard->PAGE2 ? 1 : 0;
            APPLE2Einterface::set_switches(mode, mainboard->MIXED, page);
            APPLE2Einterface::iterate();
            chrono::time_point<chrono::system_clock> now;

            auto elapsed_millis = chrono::duration_cast<chrono::milliseconds>(now - then);
            if(!run_fast || pause_cpu)
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
            } else if(strcmp(line, "fast") == 0) {
                printf("run flat out\n");
                run_fast = true;
                continue;
            } else if(strcmp(line, "slow") == 0) {
                printf("run 1mhz\n");
                run_fast = false;
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
            if(debug & DEBUG_DECODE) {
                string dis = read_bus_and_disassemble(bus, cpu.pc);
                printf("%s\n", dis.c_str());
            }
            
            if(use_fake6502) {
                clockticks6502 = 0;
                step6502();
                clk += clockticks6502;
            } else {
                cpu.cycle(bus);
            }
            mainboard->sync();

            APPLE2Einterface::DisplayMode mode = mainboard->TEXT ? APPLE2Einterface::TEXT : (mainboard->HIRES ? APPLE2Einterface::HIRES : APPLE2Einterface::LORES);
            int page = mainboard->PAGE2 ? 1 : 0;
            APPLE2Einterface::set_switches(mode, mainboard->MIXED, page);
            APPLE2Einterface::iterate();
        }
    }

    stop_keyboard();
    APPLE2Einterface::shutdown();
}
