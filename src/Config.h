#pragma once

#include <string>
#include <map>

class Config {
public:
    std::string err;

    bool open(const std::string &path);

    bool get_int(const std::string &section, const std::string &key, int &out, int def = 0) const;

    bool get_double(const std::string &section, const std::string &key, double &out, double def = 0.0) const;

    bool get_string(const std::string &section, const std::string &key, std::string &out,
                    const std::string &def = "") const;

    bool get_bool(const std::string &section, const std::string &key, bool &out, bool def = false) const;

    void close();

private:
    std::map<std::string, std::map<std::string, std::string> > data;
};
