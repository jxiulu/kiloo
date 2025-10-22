#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctype.h>
#include <curl/curl.h>
#include <errno.h>
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

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

constexpr std::string VERSION = "0.0.0.1";
constexpr int TAB_SIZE = 8;

#define ENTERALTBUF "\x1b[?1049h"
#define LEAVEALTBUF "\x1b[?1049l"
#define CLEARSCREEN "\x1b[2J" // size 4
#define RESETCURSOR "\x1b[H"  // size 3
#define WIPESCROLLBACK "\x1b[3J"
#define RESETCURSOR "\x1b[H"
#define INVERTCOLOUR "\x1b[7m" // size 4
#define NORMALCOLOUR "\x1b[m"  // size 3
#define CLEARLINE "\x1b[K"     // size 3
#define HIDECURSOR "\x1b[?25l" // size 6
#define SHOWCURSOR "\x1b[?25h" // size 6

#define CONTROL(k) ((k) & 0x1f)

typedef int echar;

struct thing {
    int x;
    int y;
};

enum Key {
    BACKSPACE = 127,
    LEFTARROW = 1000,
    RIGHTARROW,
    UPARROW,
    DOWNARROW,
    DEL,
    HOME,
    END,
    PAGEUP,
    PAGEDOWN,
    ESC,
};

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

  public:
    std::vector<Line> lines;
    struct editorspace {
        int lineid;
        int charid;
    } pointer;
    std::string fileName;

    Editor() : edirty(0), lines{}, pointer{0, 0}, fileName{} {}

    int numlines() { return static_cast<int>(lines.size()); }

    Line &line(int id) {
        if (lines.empty()) {
            throw std::out_of_range("line(): no lines to reference!");
        } // dereferencing a nonexistent line will crash
        id = std::clamp(id, 0, numlines() - 1);
        return lines[id];
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

        charid = std::clamp(charid, 0, line(lineid).size());
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
            Line &currentln = line(pointer.lineid);
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

        Line &current = line(pointer.lineid);
        if (pointer.charid > 0) {
            lines[pointer.lineid].delchar(pointer.charid - 1);
            pointer.charid--;
        } else {
            const int line_above = pointer.lineid - 1;
            Line &previous = line(line_above);
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

class Terminal {
  private:
    thing winsize;
    struct termios original;

  public:
    struct thing find_cursor() {
        char seq[32];
        unsigned int index = 0;
        struct thing pos;

        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return {-1, 0};
        if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
            return {-1, 0};

        while (index < sizeof(seq) - 1) {
            if (read(STDIN_FILENO, &seq[index], 1) != 1)
                break;
            if (seq[index] == 'R')
                break;
            index++;
        }

        seq[index] = '\0';

        if (seq[0] != '\x1b' || seq[1] != '[')
            return {-1, 0};
        if (sscanf(&seq[2], "%d;%d", &pos.y, &pos.x) != 2)
            return {-1, 0};
        return pos;
    }

    Terminal() {
        if (tcgetattr(STDIN_FILENO, &original) == -1)
            crash("tcgettattr");
        struct winsize win;

        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) == -1 || win.ws_col == 0) {
            winsize = find_cursor();
        } else {
            winsize.x = win.ws_col;
            winsize.y = win.ws_row;
        }
    }

    struct thing window_size() { return winsize; }

    void update_winsize() {
        struct winsize win;

        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) == -1 || win.ws_col == 0) {
            winsize = find_cursor();
        } else {
            winsize.x = win.ws_col;
            winsize.y = win.ws_row;
        }
    }

    void disable_raw() {
        write(STDOUT_FILENO, LEAVEALTBUF, 8);
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) == -1)
            crash("tcsetattr");
    }

    void enable_raw() {
        struct termios newterm = original;
        newterm.c_iflag &= ~(ICRNL | IXON | INPCK | ISTRIP | IXON);
        newterm.c_oflag &= ~(OPOST); // disable output processing
        newterm.c_cflag |= (CS8);
        newterm.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        // ISIG = disables signals (ctrl)
        // ICANON disables canonical mode (enter to input)
        // ECHO disables key echoing
        // IXON disables ctrl s and ctrl q

        newterm.c_cc[VMIN] = 0; // min number of bytes neded for read returns
        newterm.c_cc[VTIME] =
            1; // maximum amount of time to wait before read
               // returns (in deciseconds), or the read timeout

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &newterm) == -1)
            die("tcsettattr");
    }

    Terminal &send(std::string buf) {
        write(STDOUT_FILENO, buf.c_str(), static_cast<int>(buf.size()));
        return *this;
    }

    std::string place_cursorcmd(int x, int y) {
        // takes 0-indexed values
        std::string cmd =
            "\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H";
        return cmd;
    }

    echar read_key() {
        int int_read;
        char char_read;

        // processing escape sequences

        while ((int_read = read(STDIN_FILENO, &char_read, 1)) != 1) {
            if (int_read == -1 && errno != EAGAIN) {
                disable_raw();
                die("read");
            }
        }

        if (char_read == '\x1b') {
            char sequence[3];

            if (read(STDIN_FILENO, &sequence[0], 1) != 1)
                return '\x1b';
            if (read(STDIN_FILENO, &sequence[1], 1) != 1)
                return '\x1b';
            // if an escape character is read, read two (or more) bytes into the
            // sequence buffer. If either times out, Esc was just pressed and
            // nothing else

            if (sequence[0] == '[') {
                if (sequence[1] >= '0' && sequence[1] <= '9') {
                    if (read(STDIN_FILENO, &sequence[2], 1) != 1)
                        return '\x1b';

                    if (sequence[2] ==
                        '~') { // if the byte after [ is a digit, we read
                               // another byte, which should be ~. 5 or 6
                               // corresponds to page up or page down
                        switch (sequence[1]) {
                        case '1':
                            return HOME;
                        case '3':
                            return DEL;
                        case '4':
                            return END;
                        case '5':
                            return PAGEUP;
                        case '6':
                            return PAGEDOWN;
                        case '7':
                            return HOME;
                        case '8':
                            return END;
                        }
                    }
                } else {
                    switch (sequence[1]) {
                    case 'A':
                        return UPARROW;
                    case 'B':
                        return DOWNARROW;
                    case 'C':
                        return RIGHTARROW;
                    case 'D':
                        return LEFTARROW;
                    case 'H':
                        return HOME;
                    case 'F':
                        return END;
                    }
                }

                return '\x1b';
            } else if (sequence[0] == 'O') {
                switch (sequence[1]) {
                case 'H':
                    return HOME;
                case 'F':
                    return END;
                }
            } else {
                return char_read;
            }
        }

        return char_read;
    }

    void crash(const std::string reason) {
        write(STDOUT_FILENO, CLEARSCREEN, 4);
        write(STDOUT_FILENO, RESETCURSOR, 3);
        write(STDOUT_FILENO, LEAVEALTBUF, 7);
        throw std::runtime_error(reason);
    }

    void die(const std::string reason) {
        write(STDOUT_FILENO, CLEARSCREEN, 4);
        write(STDOUT_FILENO, RESETCURSOR, 3);
        write(STDOUT_FILENO, LEAVEALTBUF, 7);
        disable_raw();
        throw std::runtime_error(reason);
    }
};

class Interface {
  private:
    Terminal &terminal;
    Editor &editor;

  public:
    std::string outbuf;
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
        std::string contents;
    };
    std::vector<struct rowindex> index;

    static constexpr auto MSGLIF = std::chrono::seconds{5};
    static constexpr int QUIT_TIMES = 3;
    static constexpr int SBARHEIGHT = 2;

    void update_index() {
        index.clear();
        if (view_size.x <= 0 || editor.numlines() == 0) {
            return;
        }

        for (int lineid = 0; lineid < editor.numlines(); lineid++) {
            Line &at = editor.line(lineid);
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
        Line &corline = editor.line(targetrow.lineid);
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
        Line &currentline = editor.line(currentrow.lineid);
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
            outbuf.push_back('~');
            padding--;
        }
        while (padding--)
            outbuf.push_back(' ');

        outbuf.append(msg);
    }

    void draw_rows() {
        for (int viewrow = 0; viewrow < view_size.y; viewrow++) {
            outbuf.append(
                CLEARLINE); // CLEARLINE clears from the cursor to the left,
                            // which may clear the last character of the column
            // put it here to stop that from happening

            const int absrow = absy(viewrow);
            const bool coldopen = absrow >= filled_rows();

            if (coldopen) {
                if (editor.numlines() == 0 && viewrow == view_size.y / 3) {
                    print_welcomemsg();
                } else {
                    outbuf.push_back('~');
                }
            } else {
                const rowindex &currentrow = index[absrow];
                Line &currentline = editor.line(currentrow.lineid);

                const int width = get_width(absrow);
                if (width > 0)
                    outbuf.append(currentline.render, currentrow.charid, width);
            }

            outbuf.append("\r\n");
        }
    }

    void draw_statusbar() {
        outbuf.append(INVERTCOLOUR);
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

        outbuf.append(left.substr(0, std::min(leftlen, view_size.x)));

        while (leftlen < view_size.x) {
            if (view_size.x - leftlen == rightlen) {
                outbuf.append(right);
                break;
            } else {
                outbuf.push_back(' ');
                leftlen++;
            }
        }

        outbuf.append(NORMALCOLOUR);
        outbuf.append("\r\n");
    }

    void draw_msgbar() {
        outbuf.append(CLEARLINE);
        if (statusmsg.empty()) {
            return;
        }

        if (std::chrono::steady_clock::now() - statusmsg_born > MSGLIF)
            return;

        outbuf.append(statusmsg.substr(
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
            terminal.send(CLEARSCREEN);
            terminal.send(RESETCURSOR);
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

        qrepeat = QUIT_TIMES;
        update_index();
    }

    void draw_screen() {
        scroll();
        outbuf.append(HIDECURSOR);
        outbuf.append(RESETCURSOR);

        terminal.update_winsize();
        view_size = terminal.window_size();
        view_size.y -= SBARHEIGHT;

        draw_rows();
        draw_statusbar();
        draw_msgbar();

        int rcx = editor.lines.size() == 0 ? 0
                                           : editor.line(editor.pointer.lineid)
                                                 .getrx(editor.pointer.charid);
        grab_cursorpos(editor.pointer.lineid, rcx);
        outbuf.append(terminal.place_cursorcmd(cursor.x, cursor.y));
        outbuf.append(SHOWCURSOR);

        terminal.send(outbuf);

        outbuf.clear();
    }

    Interface(Editor &editor, Terminal &terminal)
        : terminal(terminal), editor(editor), statusmsg(""),
          statusmsg_born(std::chrono::steady_clock::now()), view_offset{0, 0},
          view_size{0, 0}, cursor{0, 0} {
        view_size = terminal.window_size();
        view_size.y -= SBARHEIGHT;
        index.reserve(view_size.y);

        terminal.enable_raw();
        terminal.send(CLEARSCREEN);
        terminal.send(RESETCURSOR);

        set_statusmsg("^Q to quit | ^S to save");
    }

    ~Interface() { terminal.disable_raw(); }
};

int main(int argc, char *argv[]) {
    Editor editor;
    Terminal terminal;
    Interface interface(editor, terminal);

    if (argc >= 2) {
        fs::path file(argv[1]);
        editor.open(file);
    }

    while (true) {
        interface.draw_screen();
        interface.process_key();
    }

    return 0;
}
