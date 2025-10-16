#include "Config.h"

#include <cstring>
#include <fstream>
#include <regex>

bool Config::open(const std::string &path) {
    err.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        err = "Failed to open " + path + ": " + std::string(strerror(errno));
        return false;
    }

    std::string line;
    std::string current_section;

    std::regex section_re(R"(^\s*\[\s*([^\]]+)\s*\]\s*(?:[;#].*)?$)");
    std::regex keyval_re(R"(^\s*([^#;=\s]+)\s*=\s*"?\s*([^"#;]*?)\s*"?\s*(?:[;#].*)?$)");

    while (getline(file, line)) {
        if (line.empty()) continue;

        std::smatch m;
        if (regex_match(line, m, section_re)) {
            current_section = m[1].str();
            continue;
        }

        if (regex_match(line, m, keyval_re)) {
            std::string key = m[1].str();
            std::string value = m[2].str();
            data[current_section][key] = value;
        }
    }

    return true;
}

bool Config::get_int(const std::string &section, const std::string &key, int &out, int def) const {
    out = def;

    auto s = data.find(section);
    if (s == data.end()) return false;

    auto k = s->second.find(key);
    if (k == s->second.end()) return false;

    try {
        const std::string &val = k->second;
        size_t pos;
        int v = std::stoi(val, &pos);
        if (pos != val.size())
            return false; // строка содержит лишние символы
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}


bool Config::get_double(const std::string &section, const std::string &key, double &out, double def) const {
    out = def;
    auto s = data.find(section);
    if (s == data.end()) return false;

    auto k = s->second.find(key);
    if (k == s->second.end()) return false;

    try {
        const std::string &val = k->second;
        size_t pos;
        double v = std::stod(val, &pos);
        if (pos != val.size())
            return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool Config::get_string(const std::string &section, const std::string &key, std::string &out,
                        const std::string &def) const {
    out = def;
    auto s = data.find(section);
    if (s != data.end()) {
        auto k = s->second.find(key);
        if (k != s->second.end()) {
            out = k->second;
            return true;
        }
    }
    return false;
}

bool Config::get_bool(const std::string &section, const std::string &key, bool &out, bool def) const {
    out = def;
    auto s = data.find(section);
    if (s != data.end()) {
        auto k = s->second.find(key);
        if (k != s->second.end()) {
            std::string val = k->second;
            transform(val.begin(), val.end(), val.begin(), ::tolower);
            if (val == "1" || val == "true") {
                out = true;
                return true;
            }
            if (val == "0" || val == "false") {
                out = false;
                return true;
            }
            return false;
        }
    }
    return false;
}

void Config::close() { data.clear(); }
