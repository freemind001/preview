#include "Converter.h"

#include <iostream>
#include <unordered_set>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>

static const int K_UP = 0;
static const int K_DOWN = 1;
static const int K_REPEAT = 2;

static const int ANY_SHIFT = -100; // special placeholder meaning "any shift"

static const std::unordered_set<int> Keys = {
    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUAL,
    KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P, KEY_LEFTBRACE, KEY_RIGHTBRACE,
    KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE,
    KEY_BACKSLASH, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_KPASTERISK,
    KEY_SPACE, KEY_KP7, KEY_KP8, KEY_KP9, KEY_KPMINUS, KEY_KP4, KEY_KP5, KEY_KP6, KEY_KPPLUS, KEY_KP1, KEY_KP2,
    KEY_KP3, KEY_KP0, KEY_KPDOT, KEY_KPSLASH, KEY_ENTER, KEY_KPENTER
};

static const std::unordered_set<int> Shifts = {KEY_LEFTSHIFT, KEY_RIGHTSHIFT};

static const std::unordered_set<int> BufKillers = {
    BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, KEY_TAB, KEY_LEFTCTRL, KEY_LEFTALT, KEY_RIGHTCTRL, KEY_RIGHTALT, KEY_HOME,
    KEY_UP, KEY_PAGEUP, KEY_LEFT, KEY_RIGHT, KEY_END, KEY_DOWN, KEY_PAGEDOWN, KEY_INSERT
};

Converter::Converter() : conv_key(0), ls_keys{0, 0} {
}

Converter::~Converter() {
    buffer_.clear();
}

// Write key event to internal buffer
void Converter::push(int code, int value) {
    // clear the buffer if a "killer" key (like Tab, Ctrl, mouse button, etc.) is pressed
    if (is_killer(code)) {
        clear_buffer();
        return;
    }

    // if the user-defined convert key is pressed, add it to the buffer without repeats
    if (conv_key != 0 && code == conv_key && value != K_REPEAT) {
        buffer_.push_back({code, value});
        return;
    }

    // if a shift key is pressed, always add it to the buffer
    if (is_shift(code)) {
        buffer_.push_back({code, value});
        return;
    }

    // if backspace is pressed, remove the most recent non-shift key from the buffer
    if (is_backspace(code) && !is_up(value)) {
        for (int i = buffer_.size() - 1; i >= 0; --i) {
            if (!is_shift(buffer_[i].code)) {
                buffer_.erase(buffer_.begin() + i);
                break;
            }
        }
    }

    // if a regular key is pressed, add it to the buffer
    if (is_key(code) && !is_up(value)) {
        buffer_.push_back({code, 1});
    }
}


// Process the end of the buffer to check if it's time to convert.
// If conversion is needed, also remove the processed tail.
Action Converter::process() {
    if (buffer_.empty()) {
        return None;
    };

    if (conv_key == 0) {
        // converters triggered by double shifts - default

        // 1. double shift without other shift pressed
        if (buffer_matches_pattern({
            {ANY_SHIFT, K_DOWN, false},
            {ANY_SHIFT, K_DOWN, true},
            {ANY_SHIFT, K_UP, true},
            {ANY_SHIFT, K_DOWN, true},
            {ANY_SHIFT, K_UP, true}
        })) {
            trim_buffer();
            return ConvertWord;
        }

        // 2. double shift with other shift pressed
        if (buffer_matches_pattern({
            {ANY_SHIFT, K_DOWN, true},
            {ANY_SHIFT, K_DOWN, true},
            {ANY_SHIFT, K_UP, true},
            {ANY_SHIFT, K_DOWN, true},
            {ANY_SHIFT, K_UP, true},
            {ANY_SHIFT, K_UP, true}
        })) {
            trim_buffer();
            return ConvertAll;
        }
    } else {
        // converters triggered by a user-defined key

        // 1. user-defined key without shift pressed
        if (buffer_matches_pattern({
            {ANY_SHIFT, K_DOWN, false},
            {conv_key, K_DOWN, true},
            {conv_key, K_UP, true}
        })) {
            trim_buffer();
            return ConvertWord;
        }

        // 2. user-defined key with shift pressed
        if (buffer_matches_pattern({
            {ANY_SHIFT, K_DOWN, true},
            {conv_key, K_DOWN, true},
            {conv_key, K_UP, true},
            {ANY_SHIFT, K_UP, true}
        })) {
            trim_buffer();
            return ConvertAll;
        }

        // 3. user-defined key with shift pressed and released before conv_key
        if (buffer_matches_pattern({
            {ANY_SHIFT, K_DOWN, true},
            {conv_key, K_DOWN, true},
            {ANY_SHIFT, K_UP, true},
            {conv_key, K_UP, true}
        })) {
            trim_buffer();
            return ConvertAll;
        }
    }

    return None;
}

// Returns ready-to-emit buffer.
// Doesn't modify internal buffer.
std::vector<KeyEvent> Converter::convert(Action action) const {
    std::vector<KeyEvent> result;

    // switch layout
    result.push_back({ls_keys[0], K_DOWN});
    result.push_back({ls_keys[0], K_UP});
    if (ls_keys[1] != 0) {
        result.push_back({ls_keys[1], K_DOWN});
        result.push_back({ls_keys[1], K_UP});
    }

    int start_index = 0;

    // find the last word if we are converting only the last word
    if (action == ConvertWord) {
        int i = buffer_.size() - 1;

        // skip trailing SPACE and ENTER keys
        while (i >= 0 && (buffer_[i].code == KEY_SPACE ||
                          buffer_[i].code == KEY_ENTER ||
                          buffer_[i].code == KEY_KPENTER)) {
            --i;
        }

        // move backwards until we hit a SPACE, ENTER, or the beginning
        while (i >= 0 && (buffer_[i].code != KEY_SPACE &&
                          buffer_[i].code != KEY_ENTER &&
                          buffer_[i].code != KEY_KPENTER)) {
            --i;
        }

        // start of the last word
        start_index = i + 1;
    }

    // find the start of the string if we are converting the whole buffer
    if (action == ConvertAll) {
        int i = buffer_.size() - 1;

        // skip trailing ENTER keys
        while (i >= 0 && (buffer_[i].code == KEY_ENTER ||
                          buffer_[i].code == KEY_KPENTER)) {
            --i;
        }

        // move backwards until we hit an ENTER key or reach the beginning
        while (i >= 0 && (buffer_[i].code != KEY_ENTER &&
                          buffer_[i].code != KEY_KPENTER)) {
            --i;
        }

        // start of the string
        start_index = i + 1;
    }


    // send a backspace for each key
    for (int i = start_index; i < (int) buffer_.size(); ++i) {
        if (!is_shift(buffer_[i].code)) {
            result.push_back({KEY_BACKSPACE, K_DOWN});
            result.push_back({KEY_BACKSPACE, K_UP});
        }
    }

    // replay the buffer
    for (int i = start_index; i < (int) buffer_.size(); ++i) {
        result.push_back(buffer_[i]);
        if (!is_shift(buffer_[i].code)) {
            result.push_back({buffer_[i].code, K_UP});
        }
    }

    return result;
}

// Returns readable buffer.
std::string Converter::get_buffer_dump() const {
    if (buffer_.empty()) return "(empty)";

    std::string out;
    for (const auto &ev : buffer_) {
        std::string name;

        const char *keyname = libevdev_event_code_get_name(EV_KEY, ev.code);
        if (keyname)
            name = keyname;
        else
            name = std::to_string(ev.code);

        std::string state;
        switch (ev.value) {
            case K_DOWN:   state = "DOWN";   break;
            case K_UP:     state = "UP";     break;
            case K_REPEAT: state = "REPEAT"; break;
            default:       state = std::to_string(ev.value); break;
        }

        out += "<" + name + " " + state + ">";
    }

    return out;
}


void Converter::clear_buffer() {
    buffer_.clear();
}

bool Converter::is_key(int code) const {
    return Keys.count(code) != 0;
}

bool Converter::is_shift(int code) const {
    return Shifts.count(code) != 0;
}

bool Converter::is_backspace(int code) const {
    return code == KEY_BACKSPACE;
}

bool Converter::is_killer(int code) const {
    return BufKillers.count(code) != 0;
}

bool Converter::is_up(int value) const {
    return value == K_UP;
}

// Check if the tail of the buffer matches a given pattern.
// Each Pattern contains:
//   - ev.code  : key code to match (or ANY_SHIFT as a wildcard for any shift key)
//   - ev.value : key state to match (K_DOWN, K_UP, etc.)
//   - condition: expected match result (true if the event should match, false if it should *not* match)
//
// The function compares the last `pattern.size()` events in the buffer with the pattern.
// Returns true only if all events match their corresponding pattern entries according to `condition`.
bool Converter::buffer_matches_pattern(const std::vector<Pattern> &pattern) const {
    if (buffer_.size() < pattern.size()) return false;

    for (size_t i = 0; i < pattern.size(); ++i) {
        const auto &ev = buffer_[buffer_.size() - pattern.size() + i];
        const auto &p = pattern[i];
        if (((p.ev.code == ANY_SHIFT ? is_shift(ev.code) : ev.code == p.ev.code)
             && ev.value == p.ev.value) != p.condition) {
            return false;
        }
    }
    return true;
}

// Removes trailing non-key events from the buffer,
// but preserves a Shift release if it follows a regular key.
void Converter::trim_buffer() {
    while (!buffer_.empty() && !is_key(buffer_.back().code)) {
        if (is_shift(buffer_.back().code) && is_up(buffer_.back().value)) {
            if (buffer_.size() > 1 && is_key(buffer_[buffer_.size() - 2].code)) {
                break;
            }
        }
        buffer_.pop_back();
    }
}
