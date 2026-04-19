#include "ui.hpp"
#include "qrcode.hpp"
#include <stdio.h>
#include <string.h>
#include <string>
#include <switch.h>

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

void UI::showQR(const std::string& url) {
    printf("\033[2J\033[H");
    printLine(DGRAY, '-');
    putchar('\n');
    printCentered(GRAY "Scan to open NXShare in your browser" R, 36);
    putchar('\n');
    QR::print(url);
    putchar('\n');
    {
        int pad = (CON_W - (int)url.size()) / 2;
        for (int i = 0; i < pad; i++) putchar(' ');
        printf(BOLD WHITE "%s" R "\n", url.c_str());
    }
    putchar('\n');
    printLine(DGRAY, '-');
    printf("\033[%d;0H", CON_H);
    printCentered(GRAY "Press [A] to go back" R, 20);
}

void UI::drawInfo(const std::string& ip, int port, int mediaCount) {
    printf("\033[2J\033[H");

    std::string url = "http://" + ip + ":" + std::to_string(port);

    // Top separator
    printLine(DGRAY, '-');
    putchar('\n');

    // "Open the URL in your browser:"
    printCentered(GRAY "Open the URL in your browser:" R, 29);
    putchar('\n');

    // URL
    {
        int pad = (CON_W - (int)url.size()) / 2;
        for (int i = 0; i < pad; i++) putchar(' ');
        printf(BOLD WHITE "%s" R "\n", url.c_str());
    }
    putchar('\n');

    // "Or scan the QR code:"
    printCentered(GRAY "Or scan the QR code:" R, 20);
    putchar('\n');

    // QR code inline
    QR::printSmall(url);

    // Separator
    printLine(DGRAY, '-');
    putchar('\n');

    // Media count
    char plain[64], colored[128];
    snprintf(plain,   sizeof(plain),   "Media files found:  %d", mediaCount);
    snprintf(colored, sizeof(colored), "Media files found:  " BOLD WHITE "%d" R, mediaCount);
    printCentered(colored, (int)strlen(plain));
    putchar('\n');

    // Separator
    printLine(DGRAY, '-');
    putchar('\n');

    // Press [+] to exit
    printCentered(GRAY "Press [+] to exit" R, 17);

    // Credit pinned to bottom
    printf("\033[%d;0H", CON_H);
    printCentered(BOLD ORANGE "NXShare v1.4.0" R " " GRAY "---" R " " ORANGE "by musebrot <3" R, 33);
}
