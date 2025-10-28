// base editor

#pragma once

#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdarg.h>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/ttycom.h>
#include <sys/types.h>
#include <termcap.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

#include "extensions.hpp"
#include "terminal.hpp"
constexpr int TAB_SIZE = 8;

class Line {
  public:
    std::string chars;
    std::string render;
    int dirty;

    int size() { return static_cast<int>(chars.size()); }
    int length() { return static_cast<int>(render.size()); }

    void update_render() {
        // fills the render buffer
        int tabs = 0;
        int ci;

        for (ci = 0; ci < size(); ci++) {
            if (chars[ci] == '\t')
                tabs++;
        }

        render.clear();
        render.reserve(chars.size() + tabs * (TAB_SIZE - 1) + 1);

        for (char c : chars) {
            if (c == '\t') {
                render.push_back(' ');
                while (render.size() % TAB_SIZE != 0)
                    render.push_back(' ');
            } else {
                render.push_back(c);
            }
        }
    }

    void inschar(int loc, echar ch) {
        if (loc < 0 || loc > size())
            loc = size();

        chars.insert(loc, 1, ch);
        update_render();
        dirty++;
    }

    void delchar(int loc) {
        if (loc < 0 || loc >= size())
            return;

        chars.erase(loc, 1);
        update_render();
        dirty++;
    }

    void append(std::string str) {
        chars.append(str);

        update_render();
        dirty++;
    }

    int getrx(int cx) {
        int rx = 0;
        int ci;

        for (ci = 0; ci < cx; ci++) {
            if (chars[ci] == '\t')
                rx += (TAB_SIZE - 1) - (rx % TAB_SIZE);
            rx++;
        }

        return rx;
    }

    Line(std::string contents) : chars(contents), dirty(0) { update_render(); }
};

class Editor {
  private:
    int edirty;
    std::vector<Line> lines;
    struct editorspace {
        int lineid;
        int charid;
    } pointer;

  public:
    std::string fileName;

    Editor() : edirty(0), lines{}, pointer{0, 0}, fileName{} {}

    int numlines() { return static_cast<int>(lines.size()); }

    int pointer_linepos() { return pointer.lineid; }
    int pointer_charpos() { return pointer.charid; }

    Line &line_at(int index) {
        if (lines.empty()) {
            throw std::out_of_range("line_index(): no lines to reference!");
        } // dereferencing a nonexistent line will crash
        index = std::clamp(index, 0, numlines() - 1);
        return lines[index];
    }

    void point(int lineid, int charid) {
        if (lines.empty()) {
            pointer = {0, 0};
            return;
        }

        lineid = std::clamp(lineid, 0, numlines());
        if (lineid == numlines()) {
            pointer = {lineid, 0};
            return;
        }

        charid = std::clamp(charid, 0, lines[lineid].size());
        pointer = {lineid, charid};
    }

    void open(const std::string &filepath) {
        const fs::path path(filepath);
        if (!fs::exists(path))
            throw std::runtime_error("file not found: " + filepath);

        std::ifstream in(path);
        if (!in)
            throw std::runtime_error("failed to open: " + filepath);

        fileName = fs::canonical(path).string();
        lines.clear();

        std::string get;
        while (std::getline(in, get)) {
            // std::getline discards the delimiter '\n' by default
            if (!get.empty() && get.back() == '\r')
                get.pop_back();
            insln(numlines(), get);
        }

        clean();
    }

    void delln(int which) {
        if (which < 0 || which >= numlines())
            return;

        lines.erase(lines.begin() + which);
        edirty++;
    }

    void insln(int where, std::string contents) {
        if (where < 0 || where > numlines())
            return;

        lines.insert(lines.begin() + where, Line(contents));

        edirty++;
    }

    void inschar(echar ch) {
        if (pointer.lineid == numlines()) {
            insln(numlines(), "");
        }

        lines[pointer.lineid].inschar(pointer.charid, ch);
        pointer.charid++;
    }

    void insnewln_atptr() {
        if (pointer.charid == 0) {
            insln(pointer.lineid, "");
        } else {
            Line &currentln = line_at(pointer.lineid);
            std::string fragment;
            fragment = currentln.chars.substr(pointer.charid);
            currentln.chars.erase(pointer.charid);
            currentln.update_render();
            insln(pointer.lineid + 1, std::move(fragment));
        }

        pointer.lineid++;
        pointer.charid = 0;
    }

    void delchar() {
        if (pointer.lineid == numlines())
            return;
        if (pointer.charid == 0 && pointer.lineid == 0)
            return;

        Line &current = line_at(pointer.lineid);
        if (pointer.charid > 0) {
            lines[pointer.lineid].delchar(pointer.charid - 1);
            pointer.charid--;
        } else {
            const int line_above = pointer.lineid - 1;
            Line &previous = line_at(line_above);
            pointer.charid = previous.size();
            previous.append(current.chars);
            delln(pointer.lineid);
            pointer.lineid = line_above;
        }
    }

    std::string dump() {
        std::string dump;
        for (Line line : lines) {
            dump.append(line.chars);
            dump.push_back('\n');
        }
        return dump;
    }

    int dirty() {
        int count = edirty;
        for (Line &line : lines) {
            count += line.dirty;
        }
        return count;
    }

    void clean() {
        edirty = 0;
        for (Line &line : lines) { // by refernce to actually change th elines
            line.dirty = 0;
        }
    }
};
