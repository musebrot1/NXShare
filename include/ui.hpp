#pragma once
#include <string>
#include <switch.h>

#define FB_WIDTH  1280
#define FB_HEIGHT 720

// Colors — RGBA8888 little-endian: stored as 0xAABBGGRR
#define COL_BG      0xFF21211C  // dark bg
#define COL_SURFACE 0xFF2D2626  // cards
#define COL_BORDER  0xFF3F3636  // borders
#define COL_TEXT    0xFFF0EDED  // primary text
#define COL_MUTED   0xFF8A7878  // secondary text
#define COL_DIM     0xFF5A5856  // very subtle (credits)
#define COL_ACCENT  0xFFFF8F5B  // blue (stored as BGR = blue accent)
#define COL_WHITE   0xFFFFFFFF
#define COL_BLACK   0xFF000000

class UI {
public:
    UI();
    ~UI();
    void drawInfo(const std::string& ip, int port, int mediaCount);

private:
    Framebuffer m_fb;
    u32* m_buf;
    u32 m_stride;

    void clear(u32 color);
    void drawRect(int x, int y, int w, int h, u32 color);
    void drawText(const std::string& text, int x, int y, u32 color, int scale = 2);
    void drawTextCentered(const std::string& text, int y, u32 color, int scale = 2);
    void drawTextRight(const std::string& text, int rx, int y, u32 color, int scale = 2);
    int  textWidth(const std::string& text, int scale = 2);
    void drawQR(const std::string& url, int x, int y, int moduleSize);
    void drawIcon(int x, int y);
    void present();

    inline void setPixel(int x, int y, u32 color) {
        if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) return;
        m_buf[y * (m_stride / 4) + x] = color;
    }
};
