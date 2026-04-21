#include "ui.hpp"
#include "qrcode.hpp"
#include "font8x8.hpp"
#include "icon_data.hpp"
#include <string.h>
#include <stdio.h>
#include <string>

// Switch framebuffer with framebufferMakeLinear stores pixels right-to-left
// Fix by flipping x in setPixel
#define FLIP_X(x) (FB_WIDTH - 1 - (x))

UI::UI() {
    consoleExit(NULL);
    framebufferCreate(&m_fb, nwindowGetDefault(), FB_WIDTH, FB_HEIGHT,
                      PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&m_fb);
}

UI::~UI() {
    framebufferClose(&m_fb);
}

void UI::clear(u32 color) {
    int n = FB_WIDTH * FB_HEIGHT;
    for (int i = 0; i < n; i++) m_buf[i] = color;
}

void UI::drawRect(int x, int y, int w, int h, u32 color) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            setPixel(x + dx, y + dy, color);
}

// Draw text — font is drawn left-to-right but setPixel flips x internally
void UI::drawText(const std::string& text, int x, int y, u32 color, int scale) {
    int cx = x;
    for (char c : text) {
        if (c < 32 || c > 126) { cx += (8*scale + scale); continue; }
        const uint8_t* glyph = FONT8x8[(int)(c - 32)];
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                if (glyph[row] & (1 << col)) {
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            setPixel(cx + col*scale + sx, y + row*scale + sy, color);
                }
            }
        }
        cx += 8*scale + scale;
    }
}

// Text width in pixels
int UI::textWidth(const std::string& text, int scale) {
    return (int)text.size() * (8*scale + scale);
}

void UI::drawTextCentered(const std::string& text, int y, u32 color, int scale) {
    int w = textWidth(text, scale);
    drawText(text, (FB_WIDTH - w) / 2, y, color, scale);
}

void UI::drawTextRight(const std::string& text, int rx, int y, u32 color, int scale) {
    int w = textWidth(text, scale);
    drawText(text, rx - w, y, color, scale);
}

void UI::drawIcon(int x, int y) {
    for (int iy = 0; iy < ICON_H; iy++)
        for (int ix = 0; ix < ICON_W; ix++)
            setPixel(x + ix, y + iy, ICON_DATA[iy * ICON_W + ix]);
}

void UI::drawQR(const std::string& url, int originX, int originY, int moduleSize) {
    QR::Mat m;
    if (!QR::build(url, m)) return;

    int border = 3;
    int totalPx = (m.N + border*2) * moduleSize;
    int ox = (originX < 0) ? (FB_WIDTH - totalPx) / 2 : originX;

    drawRect(ox, originY, totalPx, totalPx, COL_WHITE);
    for (int y = 0; y < m.N; y++)
        for (int x = 0; x < m.N; x++)
            drawRect(ox + (x+border)*moduleSize, originY + (y+border)*moduleSize,
                     moduleSize, moduleSize, m.get(x,y) ? COL_BLACK : COL_WHITE);
}

void UI::present() {
    framebufferEnd(&m_fb);
}

void UI::drawInfo(const std::string& ip, int port, int mediaCount) {
    m_buf = (u32*)framebufferBegin(&m_fb, &m_stride);
    clear(COL_BG);

    std::string url = "http://" + ip + ":" + std::to_string(port);
    char countStr[32];
    snprintf(countStr, sizeof(countStr), "%d", mediaCount);

    // ── Header bar ────────────────────────────────────────────────────────
    int hdrH = 80;
    drawRect(0, 0, FB_WIDTH, hdrH, COL_SURFACE);
    drawRect(0, hdrH-1, FB_WIDTH, 1, COL_BORDER);

    // App icon — centered in header
    drawIcon((FB_WIDTH - ICON_W) / 2, (hdrH - ICON_H) / 2);

    // ── Centered single-column layout ────────────────────────────────────
    int qrSize = 9; // 27 modules * 9px = 243px QR
    int qrTotalPx = (21 + 6) * qrSize; // 243px

    int contentY = hdrH + 30;

    // "Open the URL in your browser"
    drawTextCentered("Open the URL in your browser", contentY, COL_MUTED, 2);

    // URL
    drawTextCentered(url, contentY + 32, COL_ACCENT, 2);

    // Divider
    int divW = 600;
    drawRect((FB_WIDTH - divW) / 2, contentY + 62, divW, 1, COL_BORDER);

    // "or scan the QR code"
    drawTextCentered("or scan the QR code", contentY + 74, COL_MUTED, 2);

    // QR code centered
    int qrY = contentY + 106;
    drawQR(url, -1, qrY, qrSize); // -1 = auto-center

    // Media count — vertically centered between QR bottom and footer
    int footerTop = FB_HEIGHT - 50;
    int qrBottom  = qrY + qrTotalPx;
    // Block height: 1 (divider) + 14 + 18 (label) + 14 + 40 (number) = ~87px
    int blockH = 87;
    int blockTop = qrBottom + (footerTop - qrBottom - blockH) / 2;
    drawRect((FB_WIDTH - divW) / 2, blockTop, divW, 1, COL_BORDER);
    drawTextCentered("Media files found", blockTop + 14, COL_MUTED, 2);
    drawTextCentered(countStr, blockTop + 40, COL_TEXT, 5);

    // ── Bottom bar ────────────────────────────────────────────────────────
    int barY = FB_HEIGHT - 50;
    drawRect(0, barY, FB_WIDTH, 1, COL_BORDER);
    drawRect(0, barY + 1, FB_WIDTH, FB_HEIGHT - barY - 1, COL_SURFACE);

    drawTextCentered("Press  [+]  to exit", barY + 8, COL_MUTED, 2);
    drawTextCentered("NXShare v1.5.0  ---  by musebrot <3", barY + 30, COL_DIM, 2);

    present();
}
