#pragma once
#include <string>

class UI {
public:
    UI();
    ~UI();
    void drawInfo(const std::string& ip, int port, int mediaCount);
    void showQR(const std::string& url);
};
