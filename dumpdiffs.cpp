#include <cstdio>
#include <cstring>
#include <algorithm>

int main(int argc, char **argv)
{
    FILE *dump1 = fopen(argv[1], "r");
    FILE *dump2 = fopen(argv[2], "r");

    int fieldSize = std::max(strlen(argv[1]), strlen(argv[2]));

    char line1[512];
    char line2[512];

    time_t then = time(0);
    int linecount = 0;
    size_t bytecount = 0;

    unsigned int clockhigh = 0;
    unsigned int clocklow = 0;

    while(fgets(line1, sizeof(line1) - 1, dump1)) {
        bytecount += strlen(line1);
        line1[strlen(line1) - 1] = '\0';

        if(strncmp(line1, "clock", 5) == 0) {
            if(sscanf(line1, "clock = %u, %u", &clockhigh, &clocklow) != 2) {
                printf("Failed to read clock values\n");
                exit(1);
            }
        }

        fgets(line2, sizeof(line2) - 1, dump2);
        line2[strlen(line2) - 1] = '\0';

        if(strcmp(line1, line2) != 0) {
            printf("line %d differed; clock %u, %u\n", linecount, clockhigh, clocklow);
            printf("    %*s : %s\n", fieldSize, argv[1], line1);
            printf("    %*s : %s\n", fieldSize, argv[2], line2);
            exit(1);
        }

        time_t now = time(0);
        if(now > then) {
            then = now;
            printf("byte %zd, clock %u, %u\n", bytecount, clockhigh, clocklow); 
        }

        linecount++;
    }
}
