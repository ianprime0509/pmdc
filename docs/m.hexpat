#pragma endian little

import std.mem;

enum ModuleType : u8 {
    Original = 0,
    X68000   = 1,
    IbmPc    = 2,
};

using Address = u16;

fn addr(u16 offset) {
    return 1;
};

struct KeyonDelay {
    u8 slot_number;
    u8 delay;
};

struct TempoSetting {
    u8 amount;
    
    match (amount) {
        (0xFD ... 0xFF): u8 data;
    }
};

struct Command {
    u8 command [[color("00FF00")]];
    
    match (command) {
        (0x00 ... 0x7F): u8 note_length;
        (0x80): break;
        (0xB1): u8 q_min_note_length;
        (0xB2): s8 master_modulation;
        (0xB3): u8 q_max_cut;
        (0xB4): Address ppz_extend[8];
        (0xB5): KeyonDelay keyon_delay;
        // TODO: refine
        (0xB6): u8 data;
        (0xB7): u8 data;
        (0xB8): u8 data[2];
        (0xB9): u8 data;
        (0xBA): u8 data;
        (0xBB): u8 data;
        (0xBC): u8 data;
        (0xBD): u8 data[2];
        (0xBE): u8 data;
        // TODO: 0xC0
        // 0xC1 has no data
        (0xC2): u8 data;
        (0xC3): u8 data[2];
        (0xC4): u8 data;
        (0xC5): u8 data;
        (0xC6): Address fm3_extend[3];
        (0xC7): u8 data[3];
        (0xC8): u8 data[3];
        (0xC9): u8 data;
        (0xCA): u8 data;
        (0xCB): u8 data;
        (0xCC): u8 data;
        (0xCD): u8 data[5];
        (0xCE): u16 data[3];
        (0xCF): u8 data;
        (0xD0): u8 data;
        (0xD1): u8 unused;
        (0xD2): u8 data;
        (0xD3): u8 data;
        (0xD4): u8 data;
        (0xD5): u16 data;
        (0xD6): u8 data[2];
        (0xD7): u8 data;
        (0xD8): u8 data;
        (0xD9): u8 data;
        (0xDA): u8 data[3];
        (0xDB): u8 data;
        (0xDC): u8 data;
        (0xDD): u8 data;
        (0xDE): u8 data;
        (0xDF): u8 data;
        (0xE0): u8 data;
        (0xE1): u8 data;
        (0xE2): u8 data;
        (0xE3): u8 data;
        (0xE4): u8 data;
        (0xE5): u8 data[2];
        (0xE6): u8 data;
        (0xE7): u8 data;
        (0xE8): u8 data;
        (0xE9): u8 data;
        (0xEA): u8 data;
        (0xEB): u8 data;
        (0xEC): u8 data;
        (0xED): u8 data;
        (0xEE): u8 data;
        (0xEF): u8 data[2];
        (0xF0): u8 data[4];
        (0xF1): u8 data;
        (0xF2): u8 data[4];
        // 0xF3, 0xF4: no data
        (0xF5): u8 data;
        // 0xF6: no data
        (0xF7): u16 data;
        (0xF8): u8 data[4];
        (0xF9): u16 data;
        // 0xFB: no data
        (0xFC): TempoSetting data;
        (0xFD): u8 data;
        (0xFE): u8 data;
        (0xFF): u8 data;
    }
};

struct PartPointer {
    Command *commands[0x10000] : Address [[pointer_base("addr"), inline]];
};

ModuleType module_type @ 0x0;
PartPointer part_a @ 0x01;
PartPointer part_b @ 0x03;
PartPointer part_c @ 0x05;
PartPointer part_d @ 0x07;
PartPointer part_e @ 0x09;
PartPointer part_f @ 0x0B;
PartPointer part_g @ 0x0D;
PartPointer part_h @ 0x0F;
PartPointer part_i @ 0x11;
PartPointer part_j @ 0x13;
PartPointer part_k @ 0x15;