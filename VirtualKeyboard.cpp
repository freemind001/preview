#include "VirtualKeyboard.h"

#include <cstring>
#include <unistd.h>

VirtualKeyboard::VirtualKeyboard() : dev_(nullptr), uidev_(nullptr) {}

VirtualKeyboard::~VirtualKeyboard() {
    if (uidev_) libevdev_uinput_destroy(uidev_);
    if (dev_) libevdev_free(dev_);
}

bool VirtualKeyboard::init() {
    err.clear();

    dev_ = libevdev_new();
    if (!dev_) {
        err = "Failed to init libevdev";
        return false;
    }

    libevdev_set_name(dev_, name);
    libevdev_set_id_bustype(dev_, bustype);
    libevdev_set_id_vendor(dev_, vendor);
    libevdev_set_id_product(dev_, product);
    libevdev_set_id_version(dev_, version);

    libevdev_enable_event_type(dev_, EV_KEY);
    libevdev_enable_event_type(dev_, EV_SYN);

    for (int key = 0; key < 256; ++key) {
        libevdev_enable_event_code(dev_, EV_KEY, key, nullptr);
    }

    if (libevdev_uinput_create_from_device(dev_, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev_) < 0) {
        err = "Failed to initialize virtual keyboard: " + std::string(strerror(errno));
        libevdev_free(dev_);
        dev_ = nullptr;
        return false;
    }

    const char* node = nullptr;
    for (int i = 0; i < 100; ++i) { // wait for device setup up to ~10s
        node = libevdev_uinput_get_devnode(uidev_);
        if (node && access(node, F_OK) == 0) {
            return true;
        }
        usleep(100 * 1000); // 100 ms
    }

    err = "Timed out waiting for virtual keyboard to be ready";
    return false;
}

std::string VirtualKeyboard::get_uid() const {
    std::size_t hash = std::hash<std::string>{}(name);

    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "%04x:%04x:%04x:%04x:%016zx",
                  bustype, vendor, product, version, hash);

    return buf;
}

void VirtualKeyboard::emit_key(int code, int value) {
    if (!uidev_) return;

    libevdev_uinput_write_event(uidev_, EV_KEY, code, value);
    libevdev_uinput_write_event(uidev_, EV_SYN, SYN_REPORT, 0);

    usleep(delay * 1000);
}