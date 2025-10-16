#include "DeviceManager.h"

#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <sys/inotify.h>

DeviceManager::DeviceManager()
    : inotify_fd_(-1) {
}

DeviceManager::~DeviceManager() {
    if (inotify_fd_ != -1) {
        close(inotify_fd_);
    }
    while (!events_.empty()) events_.pop();
}

int DeviceManager::init() {
    err.clear();

    inotify_fd_ = inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ == -1) {
        err = "Failed to initialize inotify: " + std::string(strerror(errno));
        return -1;
    }


    int wd = inotify_add_watch(inotify_fd_, INPUT_DEVICE_DIR.c_str(), IN_CREATE | IN_DELETE);
    if (wd == -1) {
        err = "Failed to add inotify watch on " + INPUT_DEVICE_DIR + ": " + std::string(strerror(errno));
        close(inotify_fd_);
        inotify_fd_ = -1;
        return -1;
    }

    DIR *dir = opendir(INPUT_DEVICE_DIR.c_str());
    if (!dir) {
        err = "Failed to open input devices directory " + INPUT_DEVICE_DIR + ": " + std::string(strerror(errno));
        return -1;
    }

    dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name.find("event") != 0) continue;

        std::string path = INPUT_DEVICE_DIR + name;
        bool connected = true;

        events_.emplace(connected, path);
    }
    closedir(dir);

    return inotify_fd_;
}

bool DeviceManager::fetch(std::string &path, bool &connected) {
    err.clear();

    if (events_.empty()) {
        alignas(inotify_event) char buf[4096];
        ssize_t len = read(inotify_fd_, buf, sizeof(buf));
        if (len <= 0) {
            // No events available, just ignore
            return false;
        }

        for (char *ptr = buf; ptr < buf + len;) {
            auto *event = (struct inotify_event *) ptr;
            bool is_created = (event->mask & IN_CREATE) != 0;
            bool is_deleted = (event->mask & IN_DELETE) != 0;
            if ((is_created || is_deleted) &&
                !(event->mask & IN_ISDIR) &&
                event->len > 0 &&
                std::string(event->name).find("event") == 0)
            {
                std::string name = INPUT_DEVICE_DIR + event->name;
                events_.emplace(is_created, name);
            }

            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    if (!events_.empty()) {
        auto ev = events_.front();
        events_.pop();
        connected = ev.first;
        path = ev.second;
        return true;
    }

    return false;
}

bool DeviceManager::empty() {
    return events_.empty();
}
