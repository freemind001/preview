#pragma once

#include <string>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

class VirtualKeyboard {
public:
    VirtualKeyboard();

    ~VirtualKeyboard();

    std::string err;

    const char *name = "Easy Switcher virtual keyboard";
    int bustype = BUS_VIRTUAL;
    int vendor = 0x0777;
    int product = 0x0777;
    int version = 1;

    int delay = 10;

    bool init();

    std::string get_uid() const;

    void emit_key(int code, int value);

private:
    struct libevdev *dev_;
    struct libevdev_uinput *uidev_;
};
