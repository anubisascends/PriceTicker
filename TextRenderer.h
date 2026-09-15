/*
** TextRenderer - rasterizes a string to a premultiplied BGRA bitmap using
** DirectWrite + Direct2D + WIC. Windows-only. The output matches the byte
** order of Premiere's PrPixelFormat_BGRA_4444_32f (after /255 normalization),
** so it composites over the frame with no color-space conversion.
*/

#ifndef PRICETICKER_TEXTRENDERER_H
#define PRICETICKER_TEXTRENDERER_H

struct PT_TextBitmap {
    int            width;   // pixels
    int            height;  // pixels
    int            originX; // x of the text's top-left ink within the bitmap (padding)
    int            originY; // y of the text's top-left ink within the bitmap (padding)
    unsigned char* pixels;  // premultiplied BGRA, tightly packed (stride = width*4)
};

// Render 'text' (may contain '\n' for multiple lines). Color channels are 0..1.
// 'fontFamily' is a DirectWrite font family name (e.g. L"Segoe UI"); pass
// nullptr or an empty string to fall back to the default family.
// On success fills 'outBmp' with a freshly allocated buffer (free with
// PT_FreeTextBitmap) and returns true. Returns false on any failure.
bool PT_RenderText(const wchar_t* text,
                   const wchar_t* fontFamily,
                   float fontSizePx,
                   float r, float g, float b, float a,
                   PT_TextBitmap* outBmp);

void PT_FreeTextBitmap(PT_TextBitmap* bmp);

#endif // PRICETICKER_TEXTRENDERER_H
