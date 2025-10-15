#include "EventLoop.h"

#include <cstring>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

EventLoop::EventLoop() = default;

EventLoop::~EventLoop() {
    if (stop_command_fd_ != -1) {
        close(stop_command_fd_);
    }

    if (epoll_fd_ != -1) {
        close(epoll_fd_);
    }
}

bool EventLoop::init() {
    err.clear();

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        err = "Failed to initialize loop: " + std::string(strerror(errno));
        return false;
    }

    stop_command_fd_ = eventfd(0, EFD_NONBLOCK);
    if (stop_command_fd_ == -1) {
        err = "Failed to create stop command: " + std::string(strerror(errno));
        close(epoll_fd_);
        epoll_fd_ = -1;
        return false;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = stop_command_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, stop_command_fd_, &ev) == -1) {
        err = "Failed to add stop command to loop: " + std::string(strerror(errno));
        close(stop_command_fd_);
        close(epoll_fd_);
        stop_command_fd_ = -1;
        epoll_fd_ = -1;
        return false;
    }

    return true;
}

bool EventLoop::add_handler(int fd, Callback cb) {
    err.clear();
    if (epoll_fd_ == -1) {
        err = "Cannot add handler: Event loop not initialized.";
        return false;
    }
    if (fd < 0) {
        err = "Cannot add handler: Invalid file descriptor.";
        return false;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        err = "Failed to add handler: " + std::string(strerror(errno));
        return false;
    }

    callbacks_[fd] = std::move(cb);
    return true;
}

void EventLoop::remove_handler(int fd) {
    if (epoll_fd_ == -1 || fd < 0) return;
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    callbacks_.erase(fd);
}

bool EventLoop::run(int timeout_ms) {
    err.clear();

    stop_ = false;

    if (epoll_fd_ == -1) {
        err = "Cannot run loop: Event loop not initialized.";
        return false;
    }

    const int MAX_EVENTS = 512;
    epoll_event events[MAX_EVENTS];

    while (!stop_) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout_ms);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            err = "epoll_wait failed: " + std::string(strerror(errno));
            return false;
        }

        if (nfds == 0)
            break; // exit on timeout

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            if (fd == stop_command_fd_) {
                uint64_t tmp;
                read(stop_command_fd_, &tmp, sizeof(tmp));
                stop_ = true;
                break;
            }

            auto it = callbacks_.find(fd);
            if (it != callbacks_.end()) {
                it->second(fd);
            }
        }
    }

    return true;
}

void EventLoop::stop() {
    if (stop_command_fd_ != -1) {
        uint64_t flag = 1;
        write(stop_command_fd_, &flag, sizeof(flag));
    }
}