// tui

#pragma once

#include <algorithm>
#include <chrono>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdarg.h>
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

#include "editor.hpp"
#include "extensions.hpp"
#include "terminal.hpp"

constexpr std::string VERSION = "0.0.0.1";

class TUI {
  private:
    Terminal terminal;
    std::vector<std::unique_ptr<Extension>> extensions;
    std::optional<ExtensionHost> host;

  public:
    Editor &editor;
    std::string statusmsg;
    std::chrono::steady_clock::time_point statusmsg_born;

    struct thing view_offset;
    struct thing view_size;
    struct thing
        cursor; // location of the cursor relative to the terminal window

    struct rowindex {
        int lineid;
        int charid;
        int width;
    };
    std::vector<struct rowindex> index;

    static constexpr auto MSGLIF = std::chrono::seconds{5};
    static constexpr int QUIT_TIMES = 3;
    static constexpr int SBARHEIGHT = 2;

    void register_extension(std::unique_ptr<Extension> extension) {
        extensions.emplace_back(std::move(extension));
    }

    void update_index() {
        index.clear();
        if (view_size.x <= 0 || editor.numlines() == 0) {
            return;
        }

        for (int lineid = 0; lineid < editor.numlines(); lineid++) {
            Line &at = editor.line_at(lineid);
            const int length = at.length();

            if (length == 0) {
                index.push_back(
                    {lineid, 0, 0}); // empty lines still occupy a row
                continue;
            }

            for (int charid = 0; charid < length; charid += view_size.x) {
                const int width = std::min(view_size.x, length - charid);
                index.push_back({lineid, charid, width});
            }

            if (length % view_size.x == 0) {
                index.push_back({lineid, length, 0});
            }
        }
    }

    int filled_rows() { return static_cast<int>(index.size()); }

    rowindex &index_at(int abs_y) {
        int row = std::clamp(abs_y, 0, filled_rows() - 1);
        return index[row];
    }

    int get_width(int row) {
        if (row < 0 || row >= filled_rows())
            return 0;
        const rowindex &targetrow = index_at(row);
        Line &corline = editor.line_at(targetrow.lineid);
        const int width = std::max(0, corline.length() - targetrow.charid);
        index[row].width = std::min(width, view_size.x);
        return index[row].width;

        // gets the live width instead of a cached width inside of the index.
    }
    int find_width(int row) { return index[row].width; }

    int absy() { return cursor.y + view_offset.y; }
    int absy(int y) { return y + view_offset.y; }

    int get_charid() {
        if (index.empty())
            return 0;
        return index_at(absy()).charid + cursor.x;
    }

    void grab_cursorpos(int lineid, int charid) {
        update_index();
        if (index.empty())
            return;

        int targetrowid = 0;
        for (int i = 0; i < filled_rows(); i++) {
            struct rowindex &entry = index_at(i);
            if (entry.lineid != lineid)
                continue;

            const int width = get_width(i);
            const int rowstart = entry.charid;
            const int rowend = rowstart + width;
            const bool lastrow =
                (i + 1 >= filled_rows()) || (index_at(i + 1).lineid != lineid);

            if (charid < rowend || (charid == rowend && lastrow)) {
                targetrowid = i;
                cursor.x = std::clamp(charid - rowstart, 0, width);
                break;
            }
        }

        if (targetrowid < view_offset.y)
            view_offset.y = targetrowid;
        else if (targetrowid >= abs(view_size.y)) {
            view_offset.y = targetrowid - view_size.y + 1;
        }

        cursor.y = std::clamp(targetrowid - view_offset.y, 0, view_size.y - 1);
    }

    void scroll() {
        if (absy() < 0 || absy() >= filled_rows())
            return;

        if (cursor.y < 0) {
            view_offset.y--;
        }

        if (cursor.y > view_size.y) {
            view_offset.y++;
        }
    }

    void point_editor() {
        if (index.empty()) {
            if (editor.numlines() == 0)
                editor.point(0, 0);
            return;
        }

        const rowindex &currentrow = index_at(absy());
        Line &currentline = editor.line_at(currentrow.lineid);
        const int rctarget = currentrow.charid + cursor.x;

        int rx = 0, cx = 0;
        while (cx < currentline.size()) {
            char c = currentline.chars[cx];
            int progress = 1;
            if (c == '\t')
                progress += (TAB_SIZE - 1) - (rx % TAB_SIZE);
            if (rx + progress > rctarget)
                break;
            rx += progress;
            cx++;
        }

        editor.point(currentrow.lineid, cx);
    }

    void move_cursor(echar key) {
        update_index();
        if (index.empty()) {
            view_offset.y = 0;
            cursor = {0, 0};
            point_editor();
            return;
        }

        view_offset.y =
            std::clamp(view_offset.y, 0, std::max(0, filled_rows() - 1));

        const int maxrow = filled_rows() - 1;
        const int originalrow = absy();
        int absy_temp = std::clamp(originalrow, 0, maxrow);
        const bool clampedhigh = originalrow > maxrow;

        switch (key) {
        case LEFTARROW:
            if (cursor.x > 0) {
                cursor.x--;
            } else if (clampedhigh) {
                cursor.x = get_width(absy_temp);
            } else if (absy_temp > 0) {
                absy_temp--;
                cursor.x = get_width(absy_temp);
            }
            break;
        case RIGHTARROW:
            if (cursor.x < get_width(absy_temp)) {
                cursor.x++;
                if (cursor.x == find_width(absy_temp) &&
                    absy_temp + 1 < filled_rows()) {
                    absy_temp++;
                    cursor.x = 0;
                }
            } else if (absy_temp + 1 < filled_rows()) {
                absy_temp++;
                cursor.x = 0;
            }
            break;
        case UPARROW:
            if (absy_temp > 0) {
                absy_temp--;
                cursor.x = std::min(cursor.x, get_width(absy_temp));
            }
            break;
        case DOWNARROW:
            if (absy_temp + 1 < filled_rows()) {
                absy_temp++;
                cursor.x = std::min(cursor.x, get_width(absy_temp));
            }
            break;
        case HOME:
            cursor.x = 0;
            break;
        case END:
            cursor.x = get_width(absy_temp);
            break;
        }

        if (absy_temp < view_offset.y)
            view_offset.y = absy_temp;
        else if (absy_temp >= view_offset.y + view_size.y)
            view_offset.y = absy_temp - view_size.y + 1;

        cursor.y = absy_temp - view_offset.y;
        cursor.x = std::clamp(cursor.x, 0, get_width(absy_temp));
        point_editor();
    }

    void print_welcomemsg() {
        std::string msg = "Poop editor -- version " + VERSION;
        int msglen = static_cast<int>(msg.size());

        if (msglen > view_size.x)
            msg.resize(view_size.x);

        int padding = (view_size.x - msglen) / 2;
        if (padding) {
            terminal.append("~");
            padding--;
        }
        while (padding--)
            terminal.append(" ");

        terminal.append(msg);
    }

    void draw_rows() {
        for (int viewrow = 0; viewrow < view_size.y; viewrow++) {
            terminal << clearln;
            // CLEARLINE clears from the cursor to the left,
            // which may clear the last character of the column
            // put it here to stop that from happening

            const int absrow = absy(viewrow);
            const bool coldopen = absrow >= filled_rows();

            if (coldopen) {
                if (editor.numlines() == 0 && viewrow == view_size.y / 3) {
                    print_welcomemsg();
                } else {
                    terminal.append("~");
                }
            } else {
                const rowindex &currentrow = index[absrow];
                Line &currentline = editor.line_at(currentrow.lineid);

                const int width = get_width(absrow);
                if (width > 0) {
                    auto slice = std::string_view(currentline.render.data() +
                                                      currentrow.charid,
                                                  static_cast<size_t>(width));
                    terminal.append(slice);
                }
            }

            terminal.append("\r\n");
        }
    }

    void draw_statusbar() {
        terminal << invcolour;
        const std::string filename =
            editor.fileName.empty() ? "[ no name ]" : editor.fileName;
        const std::string modified = editor.dirty() ? "[ modified ]" : "";
        const std::string left = filename + " - " +
                                 std::to_string(editor.numlines()) + " lines " +
                                 modified;
        int leftlen = static_cast<int>(
            left.size()); // this represents the entire left length

        const std::string right = std::to_string(editor.pointer.lineid + 1) +
                                  "/" + std::to_string(editor.numlines());
        const int rightlen = static_cast<int>(right.size());
        // cursor position is 0 indexed

        terminal.append(left.substr(0, std::min(leftlen, view_size.x)));

        while (leftlen < view_size.x) {
            if (view_size.x - leftlen == rightlen) {
                terminal.append(right);
                break;
            } else {
                terminal.append(" ");
                leftlen++;
            }
        }

        terminal << normcolour;
        terminal.append("\r\n");
    }

    void draw_msgbar() {
        terminal << clearln;
        if (statusmsg.empty()) {
            return;
        }

        if (std::chrono::steady_clock::now() - statusmsg_born > MSGLIF)
            return;

        terminal.append(statusmsg.substr(
            0, std::min(view_size.x, static_cast<int>(statusmsg.size()))));
    }

    void set_statusmsg(std::string msg) {
        statusmsg = std::move(msg);
        statusmsg_born = std::chrono::steady_clock::now();
    }

    std::optional<std::string> prompt(std::string msgleft,
                                      std::optional<std::string> msgright) {
        std::string input;
        while (true) {
            set_statusmsg(msgleft + input + msgright.value_or(""));
            draw_screen();

            echar c = terminal.read_key();
            switch (c) {
            case CONTROL('h'):
            case BACKSPACE:
            case DEL:
                if (!input.empty())
                    input.pop_back();
                break;

            case '\r':
                if (!input.empty()) {
                    set_statusmsg("");
                    return input;
                }
                break;

            case '\x1b':
                set_statusmsg("");
                return std::nullopt;
                break;

            default:
                if (!iscntrl(c) && c < 128)
                    input.push_back(static_cast<char>(c));
                break;
            }
        }
    }

    void save() {
        if (editor.fileName.empty()) {
            auto name = prompt("Save as: ", " (ESC to exit)");
            if (!name) {
                set_statusmsg("Save aborted");
                return;
            }
            const fs::path path = *name;
            editor.fileName = fs::weakly_canonical(fs::absolute(path)).string();
        }

        std::string dump = editor.dump();
        std::ofstream out(editor.fileName, std::ios::binary | std::ios::trunc);
        if (!out) {
            set_statusmsg(std::string("save failed: ") + std::strerror(errno));
            return;
        }

        out.write(dump.data(), static_cast<std::streamsize>(dump.size()));
        if (!out) {
            set_statusmsg(std::string("save failed: ") + std::strerror(errno));
            return;
        }

        editor.clean();
        set_statusmsg(std::to_string(dump.size()) + " bytes written to disk");
    }

    void process_key() {
        echar key = terminal.read_key();
        static int qrepeat = QUIT_TIMES;

        switch (key) {
        case CONTROL('q'):
            if (editor.dirty() && qrepeat > 0) {
                set_statusmsg("File has unsaved changes. Press ^Q " +
                              std::to_string(qrepeat) + " more times to quit.");
                qrepeat--;
                return;
            }
            terminal.disable_raw();
            terminal << clear_screen << reset_cursor << send;
            exit(0);
            break;
        case CONTROL('s'):
            save();
            break;

        case CONTROL('l'):
        case '\x1b':
            break;

        case BACKSPACE:
        case CONTROL('h'):
        case DEL:
            if (key == DEL)
                move_cursor(RIGHTARROW);
            editor.delchar();
            break;

        case '\r':
            editor.insnewln_atptr();
            break;

        case PAGEUP:
            // TODO
            break;
        case PAGEDOWN:
            // TODO
            break;
        case HOME:
        case END:
        case LEFTARROW:
        case RIGHTARROW:
        case UPARROW:
        case DOWNARROW:
            move_cursor(key);
            break;

        default:
            editor.inschar(key);
        }

        for (auto &entry : extensions) {
            entry->on_key(key, *host);
        }

        qrepeat = QUIT_TIMES;
        update_index();
    }

    void draw_screen() {
        scroll();
        terminal << hide_cursor << reset_cursor;

        terminal.update_winsize();
        view_size = terminal.window_size();
        view_size.y -= SBARHEIGHT;

        draw_rows();
        draw_statusbar();
        draw_msgbar();

        int rcx = editor.lines.size() == 0
                      ? 0
                      : editor.line_at(editor.pointer.lineid)
                            .getrx(editor.pointer.charid);
        grab_cursorpos(editor.pointer.lineid, rcx);
        terminal << place_cursor(cursor.x, cursor.y) << show_cursor << send;
    }

    TUI(Editor &editor, Terminal terminal)
        : terminal(terminal), editor(editor), statusmsg(""),
          statusmsg_born(std::chrono::steady_clock::now()), view_offset{0, 0},
          view_size{0, 0}, cursor{0, 0} {
        view_size = terminal.window_size();
        view_size.y -= SBARHEIGHT;
        index.reserve(view_size.y);

        terminal.enable_raw();
        terminal << clear_screen << reset_cursor << send;
        set_statusmsg("^Q to quit | ^S to save");

        host.emplace(editor, *this);

        for (auto &entry : extensions) {
            entry->on_start(*host);
        }
    }

    ~TUI() { terminal.disable_raw(); }
};
