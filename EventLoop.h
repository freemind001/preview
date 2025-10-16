#pragma once

#include <functional>
#include <unordered_map>
#include <string>

class EventLoop {
public:
    using Callback = std::function<void(int)>;

    EventLoop();

    ~EventLoop();

    bool init();

    bool add_handler(int fd, Callback cb);

    void remove_handler(int fd);

    bool run(int timeout_ms = -1);

    void stop();

    std::string err;

private:
    int epoll_fd_ = -1;
    int stop_command_fd_ = -1;
    bool stop_ = false;

    std::unordered_map<int, Callback> callbacks_;
};
