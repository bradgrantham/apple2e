opcodes = (
    (0x00, 7),
    (0x01, 6),
    (0x05, 3),
    (0x06, 5),
    (0x08, 3),
    (0x09, 2),
    (0x0A, 2),
    (0x0D, 4),
    (0x0E, 6),
    (0x10, 2),
    (0x11, 5),
    (0x15, 4),
    (0x16, 6),
    (0x18, 2),
    (0x19, 4),
    (0x1D, 4),
    (0x1E, 7),
    (0x20, 6),
    (0x21, 6),
    (0x24, 3),
    (0x25, 3),
    (0x26, 5),
    (0x28, 4),
    (0x29, 2),
    (0x2A, 2),
    (0x2C, 4),
    (0x2D, 4),
    (0x2E, 6),
    (0x30, 2),
    (0x31, 5),
    (0x35, 4),
    (0x36, 6),
    (0x38, 2),
    (0x39, 4),
    (0x3D, 4),
    (0x3E, 7),
    (0x40, 6),
    (0x41, 6),
    (0x45, 3),
    (0x46, 5),
    (0x48, 3),
    (0x49, 2),
    (0x4A, 2),
    (0x4C, 3),
    (0x4D, 4),
    (0x4E, 6),
    (0x50, 2),
    (0x51, 5),
    (0x55, 4),
    (0x56, 6),
    (0x58, 2),
    (0x59, 4),
    (0x5D, 4),
    (0x5E, 7),
    (0x60, 6),
    (0x61, 6),
    (0x65, 3),
    (0x66, 5),
    (0x68, 4),
    (0x69, 2),
    (0x6A, 2),
    (0x6C, 5),
    (0x6D, 4),
    (0x6E, 6),
    (0x70, 2),
    (0x71, 5),
    (0x75, 4),
    (0x76, 6),
    (0x78, 2),
    (0x79, 4),
    (0x7D, 4),
    (0x7E, 7),
    (0x81, 6),
    (0x84, 3),
    (0x85, 3),
    (0x86, 3),
    (0x88, 2),
    (0x8A, 2),
    (0x8C, 4),
    (0x8D, 4),
    (0x8E, 4),
    (0x90, 2),
    (0x91, 6),
    (0x94, 4),
    (0x95, 4),
    (0x96, 4),
    (0x98, 2),
    (0x99, 5),
    (0x9A, 2),
    (0x9D, 5),
    (0xA0, 2),
    (0xA1, 6),
    (0xA2, 2),
    (0xA4, 3),
    (0xA5, 3),
    (0xA6, 3),
    (0xA8, 2),
    (0xA9, 2),
    (0xAA, 2),
    (0xAC, 4),
    (0xAD, 4),
    (0xAE, 4),
    (0xB0, 2),
    (0xB1, 5),
    (0xB4, 4),
    (0xB5, 4),
    (0xB6, 4),
    (0xB8, 2),
    (0xB9, 4),
    (0xBA, 2),
    (0xBC, 4),
    (0xBD, 4),
    (0xBE, 4),
    (0xC0, 2),
    (0xC1, 6),
    (0xC4, 3),
    (0xC5, 3),
    (0xC6, 5),
    (0xC8, 2),
    (0xC9, 2),
    (0xCA, 2),
    (0xCC, 4),
    (0xCD, 4),
    (0xCE, 3),
    (0xD0, 2),
    (0xD1, 5),
    (0xD5, 4),
    (0xD6, 6),
    (0xD8, 2),
    (0xD9, 4),
    (0xDD, 4),
    (0xDE, 7),
    (0xE0, 2),
    (0xE1, 6),
    (0xE4, 3),
    (0xE5, 3),
    (0xE6, 5),
    (0xE8, 2),
    (0xE9, 2),
    (0xEA, 2),
    (0xEC, 4),
    (0xED, 4),
    (0xEE, 6),
    (0xF0, 2),
    (0xF1, 5),
    (0xF5, 4),
    (0xF6, 6),
    (0xF8, 2),
    (0xF9, 4),
    (0xFD, 4),
    (0xFE, 7),
)

cycles = [-1] * 256
for (opcode, count) in opcodes:
    cycles[opcode] = count

print "{"
for count in cycles:
    print "    %d," % count
print "};"
