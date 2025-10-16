#include "InputReader.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>

InputReader::InputReader() = default;

InputReader::~InputReader() {
    for (auto &entry: devices_) {
        if (entry.second.dev) libevdev_free(entry.second.dev);
        close(entry.first);
    }
}

bool InputReader::init() {
    err.clear();
    return true;
}

std::string InputReader::make_device_uid(libevdev *dev) {
    if (!dev) return "";

    int bus = libevdev_get_id_bustype(dev);
    int vendor = libevdev_get_id_vendor(dev);
    int product = libevdev_get_id_product(dev);
    int version = libevdev_get_id_version(dev);

    const char *name = libevdev_get_name(dev);
    std::size_t hash = std::hash<std::string>{}(name ? name : "");

    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "%04x:%04x:%04x:%04x:%016zx",
                  bus, vendor, product, version, hash);

    return buf;
}

std::string InputReader::get_device_uid(int fd) {
    auto it = devices_.find(fd);
    if (it != devices_.end()) {
        return it->second.uid;
    }
    return "";
}

std::string InputReader::get_key_name(int code) {
    const char *name = libevdev_event_code_get_name(EV_KEY, code);
    return name ? name : std::to_string(code);
}

std::string InputReader::get_key_state(int value) {
    switch (value) {
        case 0: return "UP";
        case 1: return "DOWN";
        case 2: return "REPEAT";
        default: return "UNKNOWN";
    }
}
std::string InputReader::get_device_name(int fd) {
    auto it = devices_.find(fd);
    if (it != devices_.end() && it->second.dev) {
        return it->second.name;
    }
    return "";
}

int InputReader::get_device_fd(const std::string &path) {
    for (const auto &entry: devices_) {
        if (entry.second.path == path) {
            return entry.first;
        }
    }
    return -1;
}

int InputReader::add_device(const std::string &path) {
    err.clear();

    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        err = "Failed to open device " + path + ": " + std::string(strerror(errno));
        return -1;
    }

    libevdev *dev = nullptr;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        err = "Failed to init " + path + ": " + std::string(strerror(-rc));
        close(fd);
        return -1;
    }

    if (!(libevdev_has_event_type(dev, EV_KEY) &&
          (libevdev_has_event_code(dev, EV_KEY, KEY_A) ||
           libevdev_has_event_code(dev, EV_KEY, BTN_LEFT)))) {
        err = "Device is not keyboard or mouse";
        libevdev_free(dev);
        close(fd);
        return -1;
    }

    std::string uid = make_device_uid(dev);
    std::string name = libevdev_get_name(dev);

    if (blacklist_.count(uid)) {
        err = "Device is blacklisted, " + name + ", UID=" + uid;
        libevdev_free(dev);
        close(fd);
        return -1;
    }

    devices_[fd] = {dev, path, uid, name};
    return fd;
}

bool InputReader::remove_device(const std::string &path) {
    err.clear();

    for (auto it = devices_.begin(); it != devices_.end(); ++it) {
        if (it->second.path == path) {
            int fd = it->first;
            if (it->second.dev) libevdev_free(it->second.dev);
            close(fd);
            devices_.erase(it);
            return true;
        }
    }

    err = "Device not found: " + path;
    return false;
}

void InputReader::add_to_blacklist(const std::string &uid) {
    blacklist_.insert(uid);
}

bool InputReader::fetch(int fd, int &code, int &value) {
    err.clear();

    auto it = devices_.find(fd);
    if (it == devices_.end()) {
        err = "Invalid file descriptor";
        return false;
    }

    Device &device = it->second;
    input_event ev{};
    int rc;

    if (device.event_queue.empty()) {
        while (true) {
            rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

            if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
                if (ev.type == EV_KEY) device.event_queue.push_back(ev);
                else continue;
            } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
                while (true) {
                    rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
                    if (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS) {
                        if (ev.type == EV_KEY) device.event_queue.push_back(ev);
                        else continue;
                    } else break;
                }
            } else break;
        }
    }

    if (!device.event_queue.empty()) {
        ev = device.event_queue.front();
        device.event_queue.pop_front();
        code = ev.code;
        value = ev.value;
        return true;
    }

    return false;
}

bool InputReader::empty() {
    return devices_.empty();
}

void InputReader::flush() {
    for (auto &entry: devices_) {
        int code, value;
        while (fetch(entry.first, code, value)) {
            // do nothing
        }
    }
}
