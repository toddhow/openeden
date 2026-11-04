// The Slang twin of lib/view/dither.glsl (task 0072, stage f): ordered (Bayer) dithering. Change both together.
#ifndef LIB_VIEW_DITHER_SLANG
#define LIB_VIEW_DITHER_SLANG

float dither8(int x, int y, float c0)
{
    int dither[8][8] = { { 0, 32, 8, 40, 2, 34, 10, 42 }, { 48, 16, 56, 24, 50, 18, 58, 26 },
        { 12, 44, 4, 36, 14, 46, 6, 38 }, { 60, 28, 52, 20, 62, 30, 54, 22 }, { 3, 35, 11, 43, 1, 33, 9, 41 },
        { 51, 19, 59, 27, 49, 17, 57, 25 }, { 15, 47, 7, 39, 13, 45, 5, 37 }, { 63, 31, 55, 23, 61, 29, 53, 21 } };

    float limit = 0.0;
    if (x < 8)
        limit = (dither[x][y] + 1) / 64.0;

    if (c0 < limit)
        return 0.0;
    return 1.0;
}

#endif
