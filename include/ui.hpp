#pragma once
#include <string>

class UI {
public:
    UI();
    ~UI();
    
    // Draw IP address, port and media count on screen
    void drawInfo(const std::string& ip, int port, int mediaCount);
};
