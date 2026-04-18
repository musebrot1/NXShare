#include "ui.hpp"
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

// Draw URL in a styled box
static void printURLBox(const std::string& url) {
    int urlLen = (int)url.size();
    int lineW  = urlLen + 6;
    int pad    = (CON_W - lineW) / 2;
    if (pad < 0) pad = 0;

    auto line = [&]() {
        for (int i = 0; i < pad; i++) putchar(' ');
        printf(CYAN);
        for (int i = 0; i < lineW; i++) putchar('-');
        printf(R "\n");
    };

    line();
    putchar('\n');
    for (int i = 0; i < pad + 3; i++) putchar(' ');
    printf(BOLD WHITE "%s" R "\n", url.c_str());
    putchar('\n');
    line();
}

void UI::showQR(const std::string& url) {
    // webConfigShow requires applet mode - initialize web applet services
    Result rc;
    WebCommonConfig config = {};

    rc = webPageCreate(&config, url.c_str());
    if (R_FAILED(rc)) return;

    webConfigSetFooter(&config, false);
    webConfigSetPointer(&config, true);
    webConfigSetWhitelist(&config, ".*");

    WebCommonReply reply = {};
    rc = webConfigShow(&config, &reply);
    (void)rc;
}

void UI::drawInfo(const std::string& ip, int port, int mediaCount) {
    printf("\033[2J\033[H");

    // Top separator
    printLine(DGRAY, '-');

    // Vertical padding to center content
    for (int i = 0; i < 5; i++) putchar('\n');

    // Label
    printCentered(GRAY "Open in your browser:" R, 21);
    putchar('\n');

    // URL box
    std::string url = "http://" + ip + ":" + std::to_string(port);
    printURLBox(url);

    putchar('\n');

    // QR hint
    printCentered(GRAY "Press [+] to exit" R, 17);

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

    // Credit pinned to bottom
    printf("\033[%d;0H", CON_H);
    printCentered(BOLD ORANGE "NXShare v1.3.1" R " " GRAY "---" R " " ORANGE "by musebrot <3" R, 33);
}
