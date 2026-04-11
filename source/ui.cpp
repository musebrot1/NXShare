#include "ui.hpp"
#include <stdio.h>
#include <string.h>
#include <string>

UI::UI() {}
UI::~UI() {}

#define R       "\033[0m"
#define BOLD    "\033[1m"
#define ORANGE  "\033[38;5;214m"
#define CYAN    "\033[38;5;39m"
#define WHITE   "\033[97m"
#define GRAY    "\033[38;5;245m"
#define DGRAY   "\033[38;5;238m"

#define CON_W 80
#define CON_H 45

static void printCentered(const char* colored, int visibleLen) {
    int pad = (CON_W - visibleLen) / 2;
    if (pad < 0) pad = 0;
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s\n", colored);
}

static void printLine(const char* color, char c) {
    printf("%s", color);
    for (int i = 0; i < CON_W; i++) putchar(c);
    printf(R "\n");
}

void UI::drawInfo(const std::string& ip, int port, int mediaCount) {
    printf("\033[2J\033[H");

    // No title row - just a top separator
    printLine(DGRAY, '-');

    // Vertical centering: block ~7 lines, center in rows 2..44
    for (int i = 0; i < 17; i++) putchar('\n');

    // URL section
    printCentered(GRAY "Open in your browser:" R, 21);
    putchar('\n');

    // URL centered, no side borders
    std::string url = "http://" + ip + ":" + std::to_string(port);
    int urlPad = (CON_W - (int)url.size()) / 2;

    // Top border
    for (int i = 0; i < urlPad - 2; i++) putchar(' ');
    printf(CYAN);
    for (int i = 0; i < (int)url.size() + 4; i++) putchar('-');
    printf(R "\n");

    // URL line (no side bars)
    for (int i = 0; i < urlPad; i++) putchar(' ');
    printf(BOLD WHITE "%s" R "\n", url.c_str());

    // Bottom border
    for (int i = 0; i < urlPad - 2; i++) putchar(' ');
    printf(CYAN);
    for (int i = 0; i < (int)url.size() + 4; i++) putchar('-');
    printf(R "\n");

    putchar('\n');
    printLine(DGRAY, '-');
    putchar('\n');

    // Media count
    char plain[64], colored[128];
    snprintf(plain,   sizeof(plain),   "Media files found:  %d", mediaCount);
    snprintf(colored, sizeof(colored), "Media files found:  " BOLD WHITE "%d" R, mediaCount);
    printCentered(colored, (int)strlen(plain));

    putchar('\n');
    printLine(DGRAY, '-');

    // Credit pinned to bottom - with real heart symbol ♥
    printf("\033[%d;0H", CON_H);
    printCentered(BOLD ORANGE "NXShare v1.3.0" R " " GRAY "---" R " " ORANGE "by musebrot <3" R, 33);
}
