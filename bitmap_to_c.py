import sys

maxwidth = 0

print "unsigned char bitmap[] = {"

for line in sys.stdin:
    bit =  0
    width = 0

    for b in xrange(0, 40):
        if line[b * 2] == '*':
            print "255,",
        else:
            print "0,",

        width += 1
        bit += 1

        if False and bit == 7:
            print "255,",
            width += 1
            bit = 0

    maxwidth = max(width, maxwidth)
    print "// %d, %d" % ( width, bit)

print "};"
print "// width = %d" % maxwidth
