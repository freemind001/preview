#pragma once

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <libevdev/libevdev.h>

struct Device {
    libevdev *dev;
    std::string path;
    std::string uid;
    std::string name;
    std::deque<input_event> event_queue;
};

class InputReader {
public:
    InputReader();

    ~InputReader();

    std::string err;

    bool init();

    std::string make_device_uid(libevdev *dev);

    std::string get_device_uid(int fd);

    std::string get_key_name(int code);

    std::string get_key_state(int value);

    std::string get_device_name(int fd);

    int get_device_fd(const std::string &path);

    int add_device(const std::string &path);

    bool remove_device(const std::string &path);

    void add_to_blacklist(const std::string &uid);

    bool fetch(int fd, int &code, int &value);

    bool empty();

    void flush();

private:
    std::unordered_map<int, Device> devices_;
    std::unordered_set<std::string> blacklist_;
};
