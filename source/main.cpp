#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>

#include "server.hpp"
#include "gallery.hpp"
#include "ui.hpp"

#define PORT 8080

struct AppletHookContext {
    Server* server;
    std::atomic<bool>* refreshNetwork;
};

static std::string getCurrentIpAddress() {
    NifmInternetConnectionStatus status{};
    NifmInternetConnectionType type{};
    u32 strength = 0;
    if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status)) ||
        status != NifmInternetConnectionStatus_Connected)
        return "";

    u32 ip = 0;
    if (R_FAILED(nifmGetCurrentIpAddress(&ip)) || ip == 0)
        return "";

    char ipStr[32] = {};
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
        (ip>>0)&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF);
    return ipStr;
}

static void handleAppletHook(AppletHookType hook, void* param) {
    if (hook != AppletHookType_OnResume) return;

    AppletHookContext* context = static_cast<AppletHookContext*>(param);
    context->server->requestRestart();
    context->refreshNetwork->store(true);
}

int main(int argc, char* argv[]) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    romfsInit();
    socketInitializeDefault();
    nifmInitialize(NifmServiceType_User);

    printf("NXShare - Starting up...\n");

    std::string ipAddress = getCurrentIpAddress();

    // Scan gallery and start server
    Gallery gallery;
    gallery.scan();
    int mediaCount = gallery.getCount();

    Server server(PORT, &gallery);
    server.start();

    UI ui;
    ui.drawInfo(ipAddress, PORT, mediaCount);

    std::atomic<bool> refreshNetwork(false);
    AppletHookContext hookContext{&server, &refreshNetwork};
    AppletHookCookie hookCookie{};
    appletSetRestartMessageEnabled(true);
    appletHook(&hookCookie, handleAppletHook, &hookContext);

    int networkCheckFrames = 0;
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus) break;

        if (kDown & HidNpadButton_Y) {
            gallery.scan();
            mediaCount = gallery.getCount();
            ui.drawInfo(ipAddress, PORT, mediaCount);
        }

        if (refreshNetwork.exchange(false) || ++networkCheckFrames >= 60) {
            networkCheckFrames = 0;
            std::string newIpAddress = getCurrentIpAddress();
            if (newIpAddress != ipAddress) {
                ipAddress = newIpAddress;
                server.requestRestart();
                ui.drawInfo(ipAddress, PORT, mediaCount);
            }
        }

        svcSleepThread(16666666ULL);
    }

    appletUnhook(&hookCookie);
    appletSetRestartMessageEnabled(false);
    server.stop();
    nifmExit(); socketExit(); romfsExit();
    return 0;
}
