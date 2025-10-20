#pragma once

#include <vector>
#include <string>

struct KeyEvent {
    int code;
    int value;
};

struct Pattern {
    KeyEvent ev;
    bool condition;

    Pattern(int code, int value, bool cond) : ev{code, value}, condition(cond) {
    }
};

enum Action {
    None,
    ConvertWord,
    ConvertAll
};


class Converter {
public:
    int conv_key;
    int ls_keys[2];

    Converter();

    ~Converter();

    bool push(int code, int value);

    Action process();

    std::vector<KeyEvent> convert(Action action) const;

    std::string get_buffer_dump() const;

    void clear_buffer();

    bool is_key(int code) const;

    bool is_shift(int code) const;

    bool is_backspace(int code) const;

    bool is_killer(int code) const;

    bool is_up(int value) const;

    bool is_down(int value) const;

    bool is_repeat(int value) const;

private:
    std::vector<KeyEvent> buffer_;

    bool buffer_matches_pattern(const std::vector<Pattern> &pattern) const;

    void trim_buffer();
};
