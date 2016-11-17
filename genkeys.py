for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
    a = ord(c)
    print "    {'%c', {%d, %d, %d, %d}}," % (a, a + 32, a, a - 64, a - 64)

for c in "1234567890-=[]\;',./`":
    a = ord(c)
    print "    {'%c', {'%c', '?', 0, 0}}," % (a, a)

    # {'A', {'a', 'A', '', ''}}
