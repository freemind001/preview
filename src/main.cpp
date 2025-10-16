#include <algorithm>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <libgen.h>
#include <sstream>
#include <sys/stat.h>

#include "Config.h"
#include "Converter.h"
#include "DeviceManager.h"
#include "EventLoop.h"
#include "InputReader.h"
#include "VirtualKeyboard.h"

#define VERSION "0.5"
#define CONFIG_FILE "/etc/easy-switcher/default.conf"

EventLoop loop;
DeviceManager manager;
InputReader reader;
VirtualKeyboard vk;
Converter conv;
Config conf;

bool debug_mode = false;

void signal_handler(int signum) {
    const char *name = "UNKNOWN";
    switch (signum) {
        case SIGINT: name = "SIGINT";
            break;
        case SIGHUP: name = "SIGHUP";
            break;
        case SIGQUIT: name = "SIGQUIT";
            break;
        case SIGTERM: name = "SIGTERM";
            break;
    }

    std::cout << "\nGot exit signal (" << name << "). Bye." << std::endl;
    loop.stop();
}

void input_handler(int device_fd) {
    int code, value;
    while (reader.fetch(device_fd, code, value)) {
        conv.push(code, value);
        if (debug_mode) {
            std::cout << "Input event: " << reader.get_key_name(code) << " "
                    << reader.get_key_state(value) << " from: "
                    << reader.get_device_name(device_fd) << std::endl;
            std::cout << "Buffer: " << conv.get_buffer_dump() << std::endl;
        }

        Action action_needed;
        action_needed = conv.process();

        if (action_needed != None) {
            for (const auto &ev: conv.convert(action_needed)) {
                vk.emit_key(ev.code, ev.value);
                if (debug_mode) {
                    std::cout << "Output: " << reader.get_key_name(ev.code) << " "
                            << reader.get_key_state(ev.value) << std::endl;
                }
            }
            reader.flush();
            if (debug_mode) std::cout << "Buffer: " << conv.get_buffer_dump() << std::endl;
        }
    }
}

void device_handler(int watcher_fd) {
    bool connected;
    std::string path;

    while (manager.fetch(path, connected)) {
        if (connected) {
            int device_fd = reader.add_device(path);
            if (device_fd != -1) {
                if (!loop.add_handler(device_fd, input_handler)) {
                    std::cerr << loop.err << std::endl;
                } else {
                    std::string uid = reader.get_device_uid(device_fd);
                    std::string name = reader.get_device_name(device_fd);
                    if (debug_mode)
                        std::cout << "Added device " << path << ": " << name << ", UID=" << uid <<
                                std::endl;
                }
            } else {
                if (debug_mode) std::cout << "Skipped device " << path << ": " << reader.err << std::endl;
            }
        } else {
            int device_fd = reader.get_device_fd(path);
            if (device_fd != -1) {
                loop.remove_handler(device_fd);
                reader.remove_device(path);
                if (debug_mode) std::cout << "Removed device: " << path << std::endl;
            }
        }
    }
}

bool run() {
    std::cout << "Easy Switcher v" << VERSION << " started" << std::endl;

    if (debug_mode) std::cout << "Initializing..." << std::endl;

    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    if (debug_mode) std::cout << "Signal handlers set." << std::endl;

    if (loop.init()) {
        if (debug_mode) std::cout << "Event loop initialized." << std::endl;
    } else {
        std::cerr << loop.err << std::endl;
        return false;
    }

    int fd = manager.init();
    if (fd == -1) {
        std::cerr << manager.err << std::endl;
        return false;
    }
    loop.add_handler(fd, device_handler);
    if (debug_mode) std::cout << "Device manager initialized." << std::endl;

    if (reader.init()) {
        if (debug_mode) std::cout << "Input reader initialized." << std::endl;
    } else {
        std::cerr << "Failed to init InputReader" << std::endl;
        return false;
    }

    if (vk.init()) {
        std::string uid = vk.get_uid();
        reader.add_to_blacklist(uid);
        if (debug_mode) std::cout << "Virtual keyboard created: " << vk.name << ", UID=" << uid << std::endl;
    } else {
        std::cerr << vk.err << std::endl;
        return false;
    }

    if (debug_mode) std::cout << "Loading configuration..." << std::endl;

    if (conf.open(CONFIG_FILE)) {
        std::string layout_switch;
        if (!conf.get_string("Easy Switcher", "layout-switch", layout_switch)) {
            std::cerr << "Failed to parse configuration file: " << CONFIG_FILE << "\n"
                    << "Error: invalid 'layout-switch' value." << std::endl;
            return false;
        }

        if (sscanf(layout_switch.c_str(), "%d+%d", &conv.ls_keys[0], &conv.ls_keys[1]) < 1) {
            std::cerr << "Failed to parse configuration file: " << CONFIG_FILE << "\n"
                    << "Invalid 'layout-switch' value: " << layout_switch << std::endl;
            return false;
        }

        if (conv.ls_keys[0] >= 0 && conv.ls_keys[0] <= 255 && conv.ls_keys[1] >= 0 && conv.ls_keys[1] <= 255) {
            if (debug_mode) std::cout << "layout-switch=" << conv.ls_keys[0] << "+" << conv.ls_keys[1] << std::endl;
        } else {
            std::cerr << "Failed to parse configuration file: " << CONFIG_FILE << "\n"
                    << "Invalid 'layout-switch' value: Key code is out of valid range (0..255)." << std::endl;
            return false;
        }

        if (!conf.get_int("Easy Switcher", "convert-key", conv.conv_key)) {
            std::cerr << "Failed to parse configuration file: " << CONFIG_FILE << "\n"
                    << "Error: invalid 'convert-key' value." << std::endl;
            return false;
        }

        if (conv.conv_key >= 0 && conv.conv_key <= 255) {
            if (debug_mode) std::cout << "convert-key=" << conv.conv_key << std::endl;
        } else {
            std::cerr << "Failed to parse configuration file: " << CONFIG_FILE << "\n"
                    << "Error: 'convert-key' is out of valid range (0–255)." << std::endl;
            return false;
        }

        if (!conf.get_int("Easy Switcher", "delay", vk.delay)) {
            std::cerr << "Failed to parse configuration file: " << CONFIG_FILE << "\n"
                    << "Error: invalid 'delay' value." << std::endl;
            return false;
        }

        if (vk.delay > 0) {
            if (debug_mode) std::cout << "delay=" << vk.delay << std::endl;
        } else {
            std::cerr << "Failed to parse configuration file: " << CONFIG_FILE << "\n"
                    << "Error: 'delay' value is out of valid range." << std::endl;
            return false;
        }

        std::string blacklist;
        if (!conf.get_string("Easy Switcher", "blacklist", blacklist)) {
            std::cerr << "Failed to parse configuration file: " << CONFIG_FILE << "\n"
                    << "Error: invalid 'blacklist' value." << std::endl;
            return false;
        }

        if (!blacklist.empty()) {
            std::stringstream ss(blacklist);
            std::string uid;

            while (std::getline(ss, uid, ',')) {
                uid.erase(0, uid.find_first_not_of(" \t"));
                uid.erase(uid.find_last_not_of(" \t") + 1);

                if (uid.size() == 36 && std::count(uid.begin(), uid.end(), ':') == 4) {
                    reader.add_to_blacklist(uid);
                    if (debug_mode) std::cout << "Added to blacklist: " << uid << std::endl;
                } else {
                    if (debug_mode) std::cout << "Ignoring invalid UID: " << uid << std::endl;
                }
            }
        }
    } else {
        std::cerr << conf.err << std::endl;
        return false;
    }
    if (debug_mode) std::cout << "Configuration file loaded." << std::endl;

    if (debug_mode) std::cout << "Starting event loop..." << std::endl;
    if (!loop.run()) {
        std::cerr << loop.err << std::endl;
        return false;
    }

    return true;
};

bool configure() {
    // 1. Config params
    int conv_key; // key to convert text
    int ls_keys[2] = {0, 0}; // key combo that switches layout
    int delay; // processing delay (ms)
    std::string blacklist;

    std::string choice;
    std::vector<unsigned short> input_keys{};

    // 2. Read existing config
    std::cout << "Checking existing config...";

    if (conf.open(CONFIG_FILE)) {
        if (conf.get_int("Easy Switcher", "delay", delay, 10) &&
            conf.get_string("Easy Switcher", "blacklist", blacklist, "")
        ) {
            std::cout << "Done." << std::endl;
        } else {
            delay = 10;
            std::cout << "Failed.\n"
                    << CONFIG_FILE << " is corrupt. A new config file will be created." << std::endl;
        }
    } else {
        delay = 10;
        std::cout << "Failed.\n"
                << conf.err << "\n"
                << "A new config file will be created." << std::endl;
    }


    // 3. Init devices
    std::cout << "Scanning keyboards...";

    if (!loop.init()) {
        std::cerr << loop.err << std::endl;
        return false;
    }

    if (manager.init() == -1) {
        std::cerr << manager.err << std::endl;
        return false;
    }

    if (manager.empty()) {
        std::cout << "Error.\n";
        std::cerr << "No keyboards found.\n" << std::endl;
        return false;
    }

    if (!reader.init()) {
        std::cerr << reader.err << std::endl;
        return false;
    }

    bool connected;
    std::string path;
    while (manager.fetch(path, connected)) {
        int fd = reader.add_device(path);
        if (fd != -1) {
            loop.add_handler(fd, [&input_keys](int fd) {
                int code, value;
                while (reader.fetch(fd, code, value)) {
                    if (!conv.is_up(value)) {
                        std::cout << code << " " << value << std::endl;
                        input_keys.push_back(code);
                    } else {
                        loop.stop();
                    }
                }
            });
        }
    }

    if (reader.empty()) {
        std::cout << "Error.\n";
        std::cerr << "No keyboards opened for reading. Are you root?\n" << std::endl;
        return false;
    }

    std::cout << "Done.\n" << std::endl;

    // 4. Set convert key
    std::cout << "Please set the key combination you will use to correct text.\n";
    std::cout << "You can use the default combination or define your own.\n";
    std::cout << "The default combination is:\n";
    std::cout << " - double SHIFT to correct the last word;\n";
    std::cout << " - double SHIFT while holding the other SHIFT to correct the whole text.\n\n";
    std::cout << "Do you want to use the default combination? (y,n) ";

    while (true) {
        std::getline(std::cin, choice);
        if (choice == "y" || choice == "Y") {
            input_keys.push_back(0);
            break;
        }
        if (choice == "n" || choice == "N") {
            std::cout << "\nPress the key you want to use to correct text.\n"
                    << "Please DO NOT use:\n"
                    << "  - Letters and numbers: A-Z, 0-9\n"
                    << "  - Special characters: ~ - = { } ; \" , . / * - + etc.\n"
                    << "  - Keys that move cursor: ← ↑ → ↓ TAB PAGEUP PAGEDOWN etc.\n"
                    << "  - Special keys: CTRL ALT SHIFT BACKSPACE DEL etc.\n\n"
                    << "Waiting for your input..." << std::endl;
            reader.flush();
            loop.run(60000);
            break;
        }
        std::cout << "Invalid input. Please enter 'y' or 'n': ";
    }

    if (input_keys.empty()) {
        std::cout << "Timeout reached.\n" << std::endl;
        return false;
    }

    conv_key = input_keys[0];
    if (conv_key == 0) {
        std::cout << "Easy Switcher will use the default combination to correct the text - double SHIFT.\n\n";
    } else {
        std::cout << "Captured key: " << reader.get_key_name(conv_key) << "\n\n";
    }


    // 5. Set layout switcher
    std::cout << "Please specify the key that is currently used to switch the keyboard layout in your system.\n"
            << "Press the key or key combination.\n"
            << "Waiting for your input..." << std::endl;
    input_keys.clear();
    reader.flush();
    loop.run(60000);

    if (input_keys.empty()) {
        std::cout << "Timeout reached.\n" << std::endl;
        return false;
    }

    ls_keys[0] = input_keys[0];
    if (input_keys.size() == 1) {
        std::cout << "Captured key: " << reader.get_key_name(ls_keys[0]) << "\n" << std::endl;
    } else {
        ls_keys[1] = input_keys[1];
        std::cout << "Captured key combination: " << reader.get_key_name(ls_keys[0]) << "+"
                << reader.get_key_name(ls_keys[1]) << "\n" << std::endl;
    };


    // 6. Save config
    std::cout << "Saving configuration..." << std::endl;

    char config_dir[sizeof(CONFIG_FILE)];
    strcpy(config_dir, CONFIG_FILE);
    char *dir = dirname(config_dir);
    if (mkdir(dir, 0755) == -1 && errno != EEXIST) {
        std::cerr << "Failed to create directory: " << strerror(errno) << std::endl;
        return false;
    }


    std::ofstream cfg_file(CONFIG_FILE);
    if (!cfg_file) {
        std::cerr << "Failed to open config file for writing: " << CONFIG_FILE << "\n"
                << strerror(errno) << std::endl;
        return false;
    }

    cfg_file << "[Easy Switcher]\n";
    cfg_file << "# Easy Switcher configuration file.\n\n";

    cfg_file << "# Scancode of the key or key combination used to switch\n";
    cfg_file << "# the keyboard layout in your system.\n";
    cfg_file << "# Key combinations are supported; use '+' as a delimiter.\n";
    cfg_file << "# Run 'sudo showkey' to find your key scancodes.\n";
    cfg_file << "# Examples:\n";
    cfg_file << "# layout-switch=125\n";
    cfg_file << "# layout-switch=29+42\n\n";
    if (ls_keys[1] > 0) {
        cfg_file << "layout-switch=" << ls_keys[0] << "+" << ls_keys[1] << "\n\n\n";
    } else {
        cfg_file << "layout-switch=" << ls_keys[0] << "\n\n\n";
    }

    cfg_file << "# Scancode of the key used to correct the entered text.\n";
    cfg_file << "# Key combinations are not supported.\n";
    cfg_file << "# Double SHIFT is used by default; set 0 to use it.\n";
    cfg_file << "# Run 'sudo showkey' to find your key scancodes.\n";
    cfg_file << "# Example:\n";
    cfg_file << "# convert-key=0\n\n";
    cfg_file << "convert-key=" << conv_key << "\n\n\n";

    cfg_file << "# Easy Switcher waits a small delay before sending keys.\n";
    cfg_file << "# This helps your system handle all events correctly.\n";
    cfg_file << "# Smaller delay makes switching faster, but may cause errors.\n";
    cfg_file << "# If you see wrong or mixed symbols, try to increase the delay.\n";
    cfg_file << "# Default delay value is 10 ms.\n";
    cfg_file << "# Example:\n";
    cfg_file << "# delay=10\n\n";
    cfg_file << "delay=" << delay << "\n\n\n";


    cfg_file << "# If you get unwanted input from a specific device,\n";
    cfg_file << "# add its UID to the blacklist below.\n";
    cfg_file << "# Easy Switcher will ignore all blacklisted devices.\n";
    cfg_file << "# Use commas (,) to separate multiple UIDs.\n";
    cfg_file << "# Run 'sudo easy-switcher --debug' to list your devices' UIDs.\n";
    cfg_file << "# Examples:\n";
    cfg_file << "# blacklist=0000:0000:0000:0000:0000000000000000\n";
    cfg_file << "# blacklist=0000:0000:0000:0000:0000000000000000,0000:0000:0000:0000:0000000000000000\n\n";
    cfg_file << "blacklist=" << blacklist << "\n\n\n";


    cfg_file.close();

    std::cout << "Configuration is successfully saved." << std::endl;
    std::cout << "See " << CONFIG_FILE << " to edit additional parameters." << std::endl;
    return true;
};

void show_help() {
    std::cout << "Easy Switcher - keyboard layout switcher v" << VERSION << "\n"
            << "Usage: easy-switcher [option]\n"
            << "Options:\n"
            << "   -c,   --configure   configure Easy Switcher\n"
            << "   -r,   --run         run\n"
            << "   -d,   --debug       run in a debug mode\n"
            << "   -h,   --help        show this help" << std::endl;
}

int main(int argc, char *argv[]) {
    std::string option = (argc == 2) ? argv[1] : "--help";

    if (option == "-c" || option == "--configure") {
        if (!configure()) {
            std::cerr << "Configuration failed, exiting.\n";
            return EXIT_FAILURE;
        }
    } else if (option == "-r" || option == "--run") {
        if (!run()) {
            std::cerr << "Easy Switcher failed, exiting.\n";
            return EXIT_FAILURE;
        }
    } else if (option == "-d" || option == "--debug") {
        debug_mode = true;
        if (!run()) {
            std::cerr << "Easy Switcher failed, exiting.\n";
            return EXIT_FAILURE;
        }
    } else {
        show_help();
    }

    return EXIT_SUCCESS;
}
