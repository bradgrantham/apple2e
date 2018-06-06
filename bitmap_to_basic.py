import sys

print "5 HGR : HCOLOR=3"
num = 10
row = 0
for line in sys.stdin:
    for b in xrange(0, 40):
        if line[b * 2] == '*':
            print "%d HPLOT %d, %d" % (num, b, row)
            num += 10
    row += 1

