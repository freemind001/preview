#pragma once

#include <string>
#include <queue>

const std::string INPUT_DEVICE_DIR = "/dev/input/";

class DeviceManager {
public:
    DeviceManager();

    ~DeviceManager();

    std::string err;

    int init();

    bool fetch(std::string &path, bool &connected);

    bool empty();

private:
    int inotify_fd_;
    std::queue<std::pair<bool, std::string> > events_;
};
