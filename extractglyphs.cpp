#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <cmath>

#include <FreeImagePlus.h>

using namespace std;

// image_out and image_in have identical dimensions
void extract_glyph(fipImage &image_in, int g, unsigned char glyph[8][7])
{
    int glyphs_across = image_in.getWidth() / 7;
    int x = (g % glyphs_across) * 7;
    int y = (g / glyphs_across) * 8; // XXX why invert in Y?
    for(int j = 0; j < 8; j++) {
        for(int i = 0; i < 7; i++) {
            RGBQUAD result;
            image_in.getPixelColor(x + i, image_in.getHeight() - 1 - (y + j), &result);
            glyph[j][i] = (result.rgbRed > 127) ? 255 : 0;
        }
    }
}

int main(int argc, char **argv)
{
    if(argc != 2) {
	printf("usage: %s input\n", argv[0]);
	exit(EXIT_FAILURE);
    }

    fipImage image_in;
    bool success;

    if (!(success = image_in.load(argv[1]))) {
        cerr << "Failed to load image from " << argv[1] << endl;
        exit(EXIT_FAILURE);
    }

    unsigned char font[96 * 8 * 7];
    for(int k = 0; k <= 95; k++) {
        unsigned char glyph[8][7];
        extract_glyph(image_in, k, glyph);
        for(int j = 0; j < 8; j++)
            for(int i = 0; i < 7; i++) {
                font[i + j * 7 + k * 7 * 8] = glyph[j][i];
            }
    }

    printf("int font_offset = 32;\n");
    printf("unsigned char font_bytes[96 * 7 * 8] = {\n");

    for(int k = 0; k < 96; k++) {
        int c = k + 32;
        if(c == '\\')
            printf("    // %d : backslash\n", c);
        else
            printf("    // %d : %c\n", c, c);
        for(int j = 0; j < 8; j++) {
            printf("    ");
            for(int i = 0; i < 7; i++)
                printf("0x%02X,", font[i + j * 7 + k * 7 * 8]);
            printf("\n");
        }
    }

    printf("};\n");

    exit(EXIT_SUCCESS);
}

