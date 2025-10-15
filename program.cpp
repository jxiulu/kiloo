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

    void updateRender() {
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

    void insChar(int loc, echar ch) {
        if (loc < 0 || loc > size())
            loc = size();

        chars.insert(loc, 1, ch);
        updateRender();
        dirty++;
    }

    void delChar(int loc) {
        if (loc < 0 || loc >= size())
            return;

        chars.erase(loc, 1);
        updateRender();
        dirty++;
    }

    void append(std::string str) {
        chars.append(str);

        updateRender();
        dirty++;
    }

    Line(std::string contents) : chars(contents), dirty(0) { updateRender(); }
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
            insLn(numlines(), get);
        }

        clean();
    }

    void delLn(int which) {
        if (which < 0 || which >= numlines())
            return;

        lines.erase(lines.begin() + which);
        edirty++;
    }

    void insLn(int where, std::string contents) {
        if (where < 0 || where > numlines())
            return;

        lines.insert(lines.begin() + where, Line(contents));

        edirty++;
    }

    void insChar(echar ch) {
        if (pointer.lineid == numlines()) {
            insLn(numlines(), "");
        }

        lines[pointer.lineid].insChar(pointer.charid, ch);
    }

    void insNewLn() {
        if (pointer.charid == 0) {
            insLn(pointer.lineid, "");
        } else {
            Line &currentln = line(pointer.lineid);
            std::string fragment;
            fragment = currentln.chars.substr(pointer.charid);
            currentln.chars.erase(pointer.charid);
            currentln.updateRender();
            insLn(pointer.lineid + 1, std::move(fragment));
        }
    }

    void delChar() {
        if (pointer.lineid == numlines())
            return;
        if (pointer.charid == 0 && pointer.lineid == 0)
            return;

        Line &current = line(pointer.lineid);
        if (pointer.charid > 0) {
            lines[pointer.lineid].delChar(pointer.charid - 1);
        } else {
            const int above = pointer.lineid - 1;
            Line &previous = line(above);
            pointer.charid = previous.size();
            previous.append(current.chars);
            delLn(pointer.lineid);
            pointer.lineid = above;
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

class Interface {
  public:
    struct termios originalTerminal;
    std::string out;
    std::string statusMsg;
    std::chrono::steady_clock::time_point statusBorn;

    Editor &editor;

    struct thing viewoffset;
    struct thing viewsize;
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

    void die(const std::string reason) {
        write(STDOUT_FILENO, CLEARSCREEN, 4);
        write(STDOUT_FILENO, RESETCURSOR, 3);
        write(STDOUT_FILENO, LEAVEALTBUF, 7);
        disableRawMode();
        throw std::runtime_error(reason);
    }

    void updateIndex() {
        index.clear();
        if (viewsize.x <= 0 || editor.numlines() == 0) {
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

            for (int charid = 0; charid < length; charid += viewsize.x) {
                const int width = std::min(viewsize.x, length - charid);
                index.push_back({lineid, charid, width});
            }

            if (length % viewsize.x == 0) {
                index.push_back({lineid, length, 0});
            }
        }
    }

    int filledrows() { return static_cast<int>(index.size()); }

    rowindex &rowat(int abs_y) {
        int row = std::clamp(abs_y, 0, filledrows() - 1);
        return index[row];
    }

    int get_width(int row) {
        if (row < 0 || row >= filledrows())
            return 0;
        const rowindex &targetrow = rowat(row);
        Line &corline = editor.line(targetrow.lineid);
        const int width = std::max(0, corline.length() - targetrow.charid);
        index[row].width = std::min(width, viewsize.x);
        return index[row].width;

        // gets the live width instead of a cached width inside of the index.
    }
    int find_width(int row) { return index[row].width; }

    int absy() { return cursor.y + viewoffset.y; }
    int absy(int y) { return y + viewoffset.y; }

    int cur_charid() {
        if (index.empty())
            return 0;
        return rowat(absy()).charid + cursor.x;
    }

    void move_to(int lineid, int charid) {
        updateIndex();
        if (index.empty())
            return;

        int targetrowid = 0;
        for (int i = 0; i < filledrows(); i++) {
            struct rowindex &entry = rowat(i);
            if (entry.lineid != lineid)
                continue;

            const int width = get_width(i);
            const int rowstart = entry.charid;
            const int rowend = rowstart + width;
            const bool lastrow =
                (i + 1 >= filledrows()) || (rowat(i + 1).lineid != lineid);

            if (charid < rowend || (charid == rowend && lastrow)) {
                targetrowid = i;
                cursor.x = std::clamp(charid - rowstart, 0, width);
                break;
            }
        }

        if (targetrowid < viewoffset.y)
            viewoffset.y = targetrowid;
        else if (targetrowid >= abs(viewsize.y)) {
            viewoffset.y = targetrowid - viewsize.y + 1;
        }

        cursor.y = std::clamp(targetrowid - viewoffset.y, 0, viewsize.y - 1);
        point();
    }

    void scroll() {
        if (absy() < 0 || absy() >= filledrows())
            return;

        if (cursor.y < 0) {
            viewoffset.y--;
        }

        if (cursor.y > viewsize.y) {
            viewoffset.y++;
        }
    }

    void point() {
        if (index.empty()) {
            if (editor.numlines() == 0)
                editor.point(0, 0);
            return;
        }

        const rowindex &currentrow = rowat(absy());
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
        updateIndex();
        if (index.empty()) {
            viewoffset.y = 0;
            cursor = {0, 0};
            point();
            return;
        }

        viewoffset.y =
            std::clamp(viewoffset.y, 0, std::max(0, filledrows() - 1));

        const int maxrow = filledrows() - 1;
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
                    absy_temp + 1 < filledrows()) {
                    absy_temp++;
                    cursor.x = 0;
                }
            } else if (absy_temp + 1 < filledrows()) {
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
            if (absy_temp + 1 < filledrows()) {
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

        if (absy_temp < viewoffset.y)
            viewoffset.y = absy_temp;
        else if (absy_temp >= viewoffset.y + viewsize.y)
            viewoffset.y = absy_temp - viewsize.y + 1;

        cursor.y = absy_temp - viewoffset.y;
        cursor.x = std::clamp(cursor.x, 0, get_width(absy_temp));
        point();
    }

    void printWelcome() {
        std::string msg = "Poop editor -- version " + VERSION;
        int msglen = static_cast<int>(msg.size());

        if (msglen > viewsize.x)
            msg.resize(viewsize.x);

        int padding = (viewsize.x - msglen) / 2;
        if (padding) {
            out.push_back('~');
            padding--;
        }
        while (padding--)
            out.push_back(' ');

        out.append(msg);
    }

    void drawRows() {
        for (int viewrow = 0; viewrow < viewsize.y; viewrow++) {
            out.append(
                CLEARLINE); // CLEARLINE clears from the cursor to the left,
                            // which may clear the last character of the column
            // put it here to stop that from happening

            const int absrow = absy(viewrow);
            const bool coldopen = absrow >= filledrows();

            if (coldopen) {
                if (editor.numlines() == 0 && viewrow == viewsize.y / 3) {
                    printWelcome();
                } else {
                    out.push_back('~');
                }
            } else {
                const rowindex &currentrow = index[absrow];
                Line &currentline = editor.line(currentrow.lineid);

                const int width = get_width(absrow);
                if (width > 0)
                    out.append(currentline.render, currentrow.charid, width);
            }

            out.append("\r\n");
        }
    }

    void drawStatusBar() {
        out.append(INVERTCOLOUR);
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

        out.append(left.substr(0, std::min(leftlen, viewsize.x)));

        while (leftlen < viewsize.x) {
            if (viewsize.x - leftlen == rightlen) {
                out.append(right);
                break;
            } else {
                out.push_back(' ');
                leftlen++;
            }
        }

        out.append(NORMALCOLOUR);
        out.append("\r\n");
    }

    void drawMsgBar() {
        out.append(CLEARLINE);
        if (statusMsg.empty()) {
            return;
        }

        if (std::chrono::steady_clock::now() - statusBorn > MSGLIF)
            return;

        out.append(statusMsg.substr(
            0, std::min(viewsize.x, static_cast<int>(statusMsg.size()))));
    }

    void setStatusMsg(std::string msg) {
        statusMsg = std::move(msg);
        statusBorn = std::chrono::steady_clock::now();
    }

    std::optional<std::string> prompt(std::string msg) {
        std::string input;
        while (true) {
            setStatusMsg(msg + input);
            drawScreen();

            echar c = readKey();
            switch (c) {
            case CONTROL('h'):
            case BACKSPACE:
            case DEL:
                if (!input.empty())
                    input.pop_back();
                break;

            case '\r':
                if (!input.empty()) {
                    setStatusMsg("");
                    return input;
                }
                break;

            case '\x1b':
                setStatusMsg("");
                return std::nullopt;
                break;

            default:
                if (!iscntrl(c) && c < 128)
                    input.push_back(static_cast<char>(c));
                break;
            }
        }
    }

    bool getCursorPosition(int &x, int &y) {
        char buf[32];
        unsigned int index = 0;

        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return false;
        if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
            return false;

        while (index < sizeof(buf) - 1) {
            if (read(STDIN_FILENO, &buf[index], 1) != 1)
                break;
            if (buf[index] == 'R')
                break;
            index++;
        }

        buf[index] = '\0';

        if (buf[0] != '\x1b' || buf[1] != '[')
            return false;
        if (sscanf(&buf[2], "%d;%d", &y, &x) != 2)
            return false;
        return true;
    }

    bool getWindowSize() {
        struct winsize win;

        if (ioctl(1 || STDOUT_FILENO, TIOCGWINSZ, &win) == -1 ||
            win.ws_col == 0) {
            return getCursorPosition(viewsize.x, viewsize.y);
        } else {
            viewsize.x = win.ws_col;
            viewsize.y = win.ws_row;
            return true;
        }
    }

    void disableRawMode() {
        write(STDOUT_FILENO, LEAVEALTBUF, 8);
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTerminal) == -1)
            die("tcsetattr");
    }

    void enableRawMode() {
        struct termios newterm = originalTerminal;
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

    void save() {
        if (editor.fileName.empty()) {
            auto name = prompt("(ESC to exit) Save as: ");
            if (!name) {
                setStatusMsg("Save aborted");
                return;
            }
            const fs::path path = *name;
            editor.fileName = fs::weakly_canonical(fs::absolute(path)).string();
        }

        std::string dump = editor.dump();
        std::ofstream out(editor.fileName, std::ios::binary | std::ios::trunc);
        if (!out) {
            setStatusMsg(std::string("save failed: ") + std::strerror(errno));
            return;
        }

        out.write(dump.data(), static_cast<std::streamsize>(dump.size()));
        if (!out) {
            setStatusMsg(std::string("save failed: ") + std::strerror(errno));
            return;
        }

        editor.clean();
        setStatusMsg(std::to_string(dump.size()) + " bytes written to disk");
    }

    echar readKey() {
        int int_read;
        char char_read;

        // processing escape sequences

        while ((int_read = read(STDIN_FILENO, &char_read, 1)) != 1) {
            if (int_read == -1 && errno != EAGAIN)
                die("read");
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

    void processKey() {
        echar key = readKey();
        static int quitrepeat = QUIT_TIMES;

        switch (key) {
        case CONTROL('q'):
            if (editor.dirty() && quitrepeat > 0) {
                setStatusMsg("File has unsaved changes. Press ^Q " +
                             std::to_string(quitrepeat) +
                             " more times to quit.");
                quitrepeat--;
                return;
            }

            write(STDOUT_FILENO, CLEARSCREEN, sizeof(CLEARSCREEN) - 1);
            write(STDOUT_FILENO, RESETCURSOR, sizeof(RESETCURSOR) - 1);
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
            editor.delChar();
            move_cursor(LEFTARROW);
            break;

        case '\r':
            editor.insNewLn();
            move_cursor(DOWNARROW);
            move_cursor(HOME);
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

        default: {
            const bool tabbing = (key == '\t');
            const int linern = editor.pointer.lineid;
            const int charrn = editor.pointer.charid;
            const int charidrn = tabbing ? cur_charid() : 0;

            editor.insChar(key);

            if (tabbing) {
                const int tabStop = ((charidrn / TAB_SIZE) + 1) * TAB_SIZE;
                editor.point(linern, charrn + 1);
                move_to(linern, tabStop);
            } else {
                editor.point(linern, charrn + 1);
                move_cursor(RIGHTARROW);
            }
            break;
        }
        }

        quitrepeat = QUIT_TIMES;
        updateIndex();
    }

    void setCursor() {
        std::string cmd = "\x1b[" + std::to_string(cursor.y + 1) + ";" +
                          std::to_string(cursor.x + 1) + "H";
        out.append(cmd);
    }

    void drawScreen() {
        scroll();
        out.append(HIDECURSOR);
        out.append(RESETCURSOR);

        getWindowSize();
        viewsize.y -= SBARHEIGHT;

        drawRows();
        drawStatusBar();
        drawMsgBar();
        setCursor();

        out.append(SHOWCURSOR);

        write(STDOUT_FILENO, out.c_str(), static_cast<int>(out.size()));
        out.clear();
    }

    Interface(Editor &editor)
        : statusMsg(""), statusBorn(std::chrono::steady_clock::now()),
          editor(editor), viewoffset{0, 0}, viewsize{0, 0}, cursor{0, 0} {
        if (tcgetattr(STDIN_FILENO, &originalTerminal) == -1)
            die("tcgettattr");
        getWindowSize();

        viewsize.y -= SBARHEIGHT;
        index.reserve(viewsize.y);

        enableRawMode();
        // write(STDOUT_FILENO, ENTERALTBUF, 8);
        write(STDOUT_FILENO, CLEARSCREEN, 4);
        write(STDOUT_FILENO, RESETCURSOR, 3);

        setStatusMsg("^Q to quit | ^S to save");
    }

    ~Interface() { disableRawMode(); }
};

int main(int argc, char *argv[]) {
    Editor editor;
    Interface interface(editor);

    if (argc >= 2) {
        fs::path file(argv[1]);
        editor.open(file);
    }

    while (true) {
        interface.drawScreen();
        interface.processKey();
    }

    return 0;
}
