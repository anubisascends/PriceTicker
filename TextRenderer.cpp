#include "TextRenderer.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <cwchar>
#include <algorithm>

using Microsoft::WRL::ComPtr;

/* ------------------------------------------------------------------------- */

void PT_FreeTextBitmap(PT_TextBitmap* bmp)
{
    if (bmp && bmp->pixels) {
        delete[] bmp->pixels;
        bmp->pixels = nullptr;
    }
    if (bmp) {
        bmp->width = 0;
        bmp->height = 0;
    }
}

bool PT_RenderText(const wchar_t* text,
                   const wchar_t* fontFamily,
                   float fontSizePx,
                   float r, float g, float b, float a,
                   PT_TextBitmap* outBmp)
{
    if (!outBmp || !text || text[0] == L'\0' || fontSizePx <= 0.0f)
        return false;

    // Fall back to a known-present family if none was supplied.
    if (!fontFamily || fontFamily[0] == L'\0')
        fontFamily = L"Segoe UI";

    outBmp->pixels = nullptr;
    outBmp->width = 0;
    outBmp->height = 0;

    // COM may or may not be initialized on this render thread. Initialize it
    // and only balance the call if we were the ones who initialized it.
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool needUninit = SUCCEEDED(hrInit);   // false if RPC_E_CHANGED_MODE

    bool result = false;

    do {
        ComPtr<ID2D1Factory>       d2dFactory;
        ComPtr<IDWriteFactory>     dwriteFactory;
        ComPtr<IWICImagingFactory> wicFactory;

        HRESULT hr = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            d2dFactory.GetAddressOf());
        if (FAILED(hr)) break;

        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf()));
        if (FAILED(hr)) break;

        hr = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(wicFactory.GetAddressOf()));
        if (FAILED(hr)) break;

        // Text format + layout.
        ComPtr<IDWriteTextFormat> textFormat;
        hr = dwriteFactory->CreateTextFormat(
            fontFamily, nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSizePx, L"en-us",
            textFormat.GetAddressOf());
        if (FAILED(hr)) break;

        textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

        const UINT32 textLen = (UINT32)wcslen(text);
        const float  kMaxLayout = 8192.0f;

        ComPtr<IDWriteTextLayout> layout;
        hr = dwriteFactory->CreateTextLayout(
            text, textLen, textFormat.Get(),
            kMaxLayout, kMaxLayout, layout.GetAddressOf());
        if (FAILED(hr)) break;

        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        DWRITE_TEXT_METRICS metrics = {};
        hr = layout->GetMetrics(&metrics);
        if (FAILED(hr)) break;

        float textW = metrics.widthIncludingTrailingWhitespace;
        float textH = metrics.height;
        if (textW < 1.0f || textH < 1.0f) break;

        // Padding to hold a drop shadow / outline and antialiased edges.
        float shadow = std::max(2.0f, fontSizePx * 0.06f);
        int pad = (int)std::ceil(shadow) + 4;

        int bmpW = (int)std::ceil(textW) + pad * 2;
        int bmpH = (int)std::ceil(textH) + pad * 2;
        if (bmpW <= 0 || bmpH <= 0) break;

        // WIC bitmap (premultiplied BGRA) as the D2D render surface.
        ComPtr<IWICBitmap> wicBitmap;
        hr = wicFactory->CreateBitmap(
            (UINT)bmpW, (UINT)bmpH,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapCacheOnLoad,
            wicBitmap.GetAddressOf());
        if (FAILED(hr)) break;

        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            0.0f, 0.0f);

        ComPtr<ID2D1RenderTarget> rt;
        hr = d2dFactory->CreateWicBitmapRenderTarget(
            wicBitmap.Get(), rtProps, rt.GetAddressOf());
        if (FAILED(hr)) break;

        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

        ComPtr<ID2D1SolidColorBrush> fillBrush;
        ComPtr<ID2D1SolidColorBrush> shadowBrush;
        hr = rt->CreateSolidColorBrush(D2D1::ColorF(r, g, b, a),
                                       fillBrush.GetAddressOf());
        if (FAILED(hr)) break;
        hr = rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, a * 0.85f),
                                       shadowBrush.GetAddressOf());
        if (FAILED(hr)) break;

        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0, 0, 0, 0));   // fully transparent

        // Drop shadow first, then fill on top, for legibility over any video.
        D2D1_POINT_2F base = D2D1::Point2F((float)pad, (float)pad);
        D2D1_POINT_2F shadowPt = D2D1::Point2F((float)pad + shadow,
                                               (float)pad + shadow);
        rt->DrawTextLayout(shadowPt, layout.Get(), shadowBrush.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_NONE);
        rt->DrawTextLayout(base, layout.Get(), fillBrush.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_NONE);

        hr = rt->EndDraw();
        if (FAILED(hr)) break;

        // Copy the pixels out of the WIC bitmap into our own buffer.
        WICRect lockRect = { 0, 0, bmpW, bmpH };
        ComPtr<IWICBitmapLock> lock;
        hr = wicBitmap->Lock(&lockRect, WICBitmapLockRead, lock.GetAddressOf());
        if (FAILED(hr)) break;

        UINT stride = 0, bufSize = 0;
        BYTE* src = nullptr;
        hr = lock->GetStride(&stride);
        if (FAILED(hr)) break;
        hr = lock->GetDataPointer(&bufSize, &src);
        if (FAILED(hr) || !src) break;

        const int dstStride = bmpW * 4;
        unsigned char* dst = new (std::nothrow) unsigned char[(size_t)dstStride * bmpH];
        if (!dst) break;

        for (int y = 0; y < bmpH; ++y)
            std::memcpy(dst + (size_t)y * dstStride,
                        src + (size_t)y * stride,
                        dstStride);

        outBmp->pixels = dst;
        outBmp->width = bmpW;
        outBmp->height = bmpH;
        outBmp->originX = pad;   // text ink starts at (pad, pad) in the bitmap
        outBmp->originY = pad;
        result = true;

    } while (false);

    if (needUninit)
        CoUninitialize();

    return result;
}
