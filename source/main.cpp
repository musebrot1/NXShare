#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <vector>
#include <string>
#include <algorithm>

#include "server.hpp"
#include "gallery.hpp"
#include "ui.hpp"

#define PORT 8080

int main(int argc, char* argv[]) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    socketInitializeDefault();
    nifmInitialize(NifmServiceType_User);

    printf("NXShare - Starting up...\n");
    consoleUpdate(NULL);

    // Wait for network
    NifmInternetConnectionStatus status;
    NifmInternetConnectionType type;
    u32 strength;
    int retries = 0;
    while (retries < 30) {
        nifmGetInternetConnectionStatus(&type, &strength, &status);
        if (status == NifmInternetConnectionStatus_Connected) break;
        printf("Waiting for network... (%d/30)\n", retries + 1);
        consoleUpdate(NULL);
        svcSleepThread(1000000000ULL);
        retries++;
    }

    if (retries >= 30) {
        printf("ERROR: No network connection!\nMake sure WiFi is connected.\n\nPress + to exit.\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
            svcSleepThread(16666666ULL);
        }
        nifmExit(); socketExit(); consoleExit(NULL);
        return 0;
    }

    // Get IP
    char ipStr[32] = {};
    u32 ip = 0;
    nifmGetCurrentIpAddress(&ip);
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
        (ip>>0)&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF);

    // Scan gallery and start server
    Gallery gallery;
    gallery.scan();
    int mediaCount = gallery.getCount();

    Server server(PORT, &gallery, ipStr);
    server.start();

    // Draw UI once, then never print anything else
    UI ui;
    ui.drawInfo(ipStr, PORT, mediaCount);
    consoleUpdate(NULL);

    // Main loop - no printf after this point so screen stays clean
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus) break;

        if (kDown & HidNpadButton_Y) {
            gallery.scan();
            mediaCount = gallery.getCount();
            ui.drawInfo(ipStr, PORT, mediaCount);
        }

        consoleUpdate(NULL);
        svcSleepThread(16666666ULL);
    }

    server.stop();
    nifmExit(); socketExit(); consoleExit(NULL);
    return 0;
}
