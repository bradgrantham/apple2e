#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <cmath>

#include <FreeImagePlus.h>

using namespace std;

// image_out and image_in have identical dimensions
void extract_glyph(fipImage &image_in, int g, unsigned char glyph[8])
{
    int glyphs_across = image_in.getWidth() / 7;
    int x = (g % glyphs_across) * 7;
    int y = (g / glyphs_across) * 8; // XXX why invert in Y?
    for(int j = 0; j < 8; j++) {
        unsigned char byte = 0;
        for(int i = 0; i < 7; i++) {
            RGBQUAD result;
            image_in.getPixelColor(x + i, image_in.getHeight() - 1 - (y + j), &result);
            int pixel = (result.rgbRed > 127) ? 1 : 0;
            byte = byte | (pixel << i);
        }
        glyph[j] = byte;
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

    printf("int font_offset = 32;\n");
    printf("unsigned char font_bytes[][8] = {\n");

    static unsigned char font[96][8];
    for(int i = 0; i <= 95; i++) {
        extract_glyph(image_in, i, font[i]);
        printf("    {");

        for(int j = 0; j < 8; j++)
            printf("0x%02X%s", font[i][j], (j < 7) ? ", " : "");

        if(i < 95)
            printf("},\n");
        else
            printf("}\n");
    }

    printf("};\n");

    exit(EXIT_SUCCESS);
}

