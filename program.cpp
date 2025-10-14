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

void die(const std::string reason) {
    write(STDOUT_FILENO, CLEARSCREEN, 4);
    write(STDOUT_FILENO, RESETCURSOR, 3);
    write(STDOUT_FILENO, LEAVEALTBUF, 7);
    // clears screen on exit

    throw std::runtime_error(reason);
}

struct position {
    int x;
    int y;
};

struct window {
    int cols;
    int rows;
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
    // private:
    //   Editor *owner;
    //   friend class Editor;
  public:
    std::string chars;
    std::string render;
    int dirty;

    int csize() { return static_cast<int>(chars.size()); }
    int rsize() { return static_cast<int>(render.size()); }

    void updateRender() {
        // fills the render buffer
        int tabs = 0;
        int ci;

        for (ci = 0; ci < csize(); ci++) {
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
        if (loc < 0 || loc > csize())
            loc = csize();

        chars.insert(loc, 1, ch);
        updateRender();
        dirty++;
    }

    void delChar(int loc) {
        if (loc < 0 || loc >= csize())
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

    // void remove() {
    //     if (owner) {
    //         auto self = shared_from_this();
    //         owner->kill(*this);
    //     }
    // }
    //
    // Line(Editor *owner, std::string contents)
    //     : owner(owner), chars(std::move(contents)) {
    //     update();
    // }

    Line(std::string contents) : chars(contents), dirty(0) { updateRender(); }
};

class Editor {
  private:
  public:
    // std::vector<std::shared_ptr<Line>> lines;
    std::vector<Line> lines;
    struct position cursor;
    std::string fileName;
    int edirty;

    Editor() : lines{}, cursor{0, 0}, fileName{}, edirty(0) {}

    int numlines() { return static_cast<int>(lines.size()); }

    Line &lineat(int loc) { return lines[loc]; }

    // void insertLine(int where, std::string contents) {
    //     if (where < 0 || where > numlines())
    //         return;
    //
    //     auto newline = std::make_shared<Line>(this, contents);
    //     lines.insert(lines.begin() + where, newline);
    //     edirty++;
    // }
    //
    // void kill(const std::shared_ptr<Line> &line) {
    //     lines.erase(std::remove(lines.begin(), lines.end(), line),
    //     lines.end()); edirty++;
    // }

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
        if (cursor.y == numlines()) {
            insLn(numlines(), "");
        }

        lines[cursor.y].insChar(cursor.x, ch);
        cursor.x++;
    }

    void insNewLn() {
        if (cursor.x == 0) {
            insLn(cursor.y, "");
        } else {
            Line &currentln = lineat(cursor.y);
            std::string fragment;
            fragment = currentln.chars.substr(cursor.x);
            currentln.chars.erase(cursor.x);
            currentln.updateRender();
            insLn(cursor.y + 1, std::move(fragment));
        }

        cursor.y++;
        cursor.x = 0;
        // insLn marks the file dirty
    }

    void delChar() {
        if (cursor.y == numlines())
            return;
        if (cursor.x == 0 && cursor.y == 0)
            return;

        Line &current = lineat(cursor.y);
        if (cursor.x > 0) {
            lines[cursor.y].delChar(cursor.x - 1);
            cursor.x--;
        } else {
            cursor.x = lines[cursor.y - 1].csize();
            lines[cursor.y - 1].append(current.chars);
            delLn(cursor.y);
            cursor.y--;
        }
    }

    std::string toString() {
        std::string data;
        for (Line line : lines) {
            data.append(line.chars);
            data.push_back('\n');
        }
        return data;
    }

    int dirty() {
        int count = 0;
        for (Line &line : lines) {
            count += line.dirty;
        }
        count += edirty;
        return count;
    }

    void moveCursor(echar key) {
        Line *current = (cursor.y >= numlines()) ? nullptr : &lines[cursor.y];

        switch (key) {
        case LEFTARROW:
            if (cursor.x != 0) {
                cursor.x--;
            } else if (cursor.y > 0) {
                cursor.y--;
                cursor.x = lines[cursor.y].csize();
            }
            break;
        case RIGHTARROW:
            if (current && cursor.x < current->csize()) {
                cursor.x++;
            } else if (current && cursor.x == current->csize()) {
                cursor.y++;
                cursor.x = 0;
            }
            break;
        case UPARROW:
            if (cursor.y != 0) {
                cursor.y--;
            }
            break;
        case DOWNARROW:
            if (cursor.y < numlines()) {
                cursor.y++;
            }
            break;
        }

        current = (cursor.y >= numlines()) ? nullptr : &lines[cursor.y];

        int currentrowlen = current ? current->csize() : 0;
        if (cursor.x > currentrowlen) {
            cursor.x = currentrowlen;
        }
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
    std::string buffer;
    std::string statusMsg;
    std::chrono::steady_clock::time_point statusBorn =
        std::chrono::steady_clock::now();

    Editor &editor;

    struct window offset;
    struct window screen;

    int rx;

    static constexpr auto MSGLIF = std::chrono::seconds{5};
    static constexpr int QUIT_TIMES = 3;
    static constexpr int SBARHEIGHT = 2;

    void scroll() {
        rx = 0;
        if (editor.cursor.y < editor.numlines()) {
            rx = editor.lines[editor.cursor.y].getrx(editor.cursor.x);
        }

        if (editor.cursor.y < offset.rows) {
            offset.rows = editor.cursor.y; // up
        }
        if (editor.cursor.y >= offset.rows + screen.rows) {
            offset.rows = editor.cursor.y - screen.rows + 1; // down
        }

        if (rx < offset.cols) {
            offset.cols = rx; // left
        }
        if (rx >= offset.cols + screen.cols) {
            offset.cols = rx - screen.cols + 1; // right
        }
    }

    void printWelcome() {
        std::string msg = "Poop editor -- version " + VERSION;
        int msglen = static_cast<int>(msg.size());

        if (msglen > screen.cols)
            msg.resize(screen.cols);

        int padding = (screen.cols - msglen) / 2;
        if (padding) {
            buffer.push_back('~');
            padding--;
        }
        while (padding--)
            buffer.push_back(' ');

        buffer.append(msg);
    }

    void drawRows() {
        for (int y = 0; y < screen.rows; y++) {
            int currentline = y + offset.rows;
            if (currentline >= editor.numlines()) {
                if (editor.numlines() == 0 && y == screen.rows / 3) {
                    printWelcome();
                } else {
                    buffer.push_back('~');
                }
            } else {
                Line &current = editor.lineat(currentline);
                int renderedwidth = current.rsize();

                if (offset.cols < renderedwidth) {
                    const int available = renderedwidth - offset.cols;
                    const int visible = std::min(available, screen.cols);

                    buffer.append(current.render.data() + offset.cols, visible);
                }
            }

            buffer.append(
                "\x1b[K\r\n"); // clear remainder of line and add new line
        }
    }

    void drawStatusBar() {
        buffer.append(INVERTCOLOUR);
        const std::string filename =
            editor.fileName.empty() ? "[ no name ]" : editor.fileName;
        const std::string modified = editor.dirty() ? "[ modified ]" : "";
        const std::string left = filename + " - " +
                                 std::to_string(editor.numlines()) + " lines " +
                                 modified;
        int leftlen = static_cast<int>(
            left.size()); // this represents the entire left length

        const std::string right = std::to_string(editor.cursor.y + 1) + "/" +
                                  std::to_string(editor.numlines());
        const int rightlen = static_cast<int>(right.size());
        // cursor position is 0 indexed

        buffer.append(left.substr(0, std::min(leftlen, screen.cols)));

        while (leftlen < screen.cols) {
            if (screen.cols - leftlen == rightlen) {
                buffer.append(right);
                break;
            } else {
                buffer.push_back(' ');
                leftlen++;
            }
        }

        buffer.append(NORMALCOLOUR);
        buffer.append("\r\n");
    }

    void drawMsgBar() {
        buffer.append(CLEARLINE);
        if (statusMsg.empty()) {
            return;
        }

        if (std::chrono::steady_clock::now() - statusBorn > MSGLIF)
            return;

        buffer.append(statusMsg.substr(
            0, std::min(screen.cols, static_cast<int>(statusMsg.size()))));
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
            return getCursorPosition(screen.cols, screen.rows);
        } else {
            screen.cols = win.ws_col;
            screen.rows = win.ws_row;
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

        std::string dump = editor.toString();
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

    void processKeypress() {
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
                editor.moveCursor(RIGHTARROW);
            editor.delChar();
            break;

        case '\r':
            editor.insNewLn();
            break;

        case HOME:
            editor.cursor.x = 0;
            break;
        case END:
            if (editor.cursor.y < editor.numlines()) {
                editor.cursor.x = editor.lineat(editor.cursor.y)
                                      .csize(); // call lineat() to make sure
                                                // cursor.y is on a valid line
            }
            break;

        case PAGEUP:
            editor.cursor.y = offset.rows;
            for (int t = 0; t < screen.rows; t++) {
                editor.moveCursor(UPARROW);
            }
            break;
        case PAGEDOWN:
            editor.cursor.y = offset.rows + screen.rows - 1;
            if (editor.cursor.y > editor.numlines())
                editor.cursor.y = editor.numlines();

            for (int t = 0; t < screen.rows; t++) {
                editor.moveCursor(DOWNARROW);
            }
            break;

        case LEFTARROW:
        case RIGHTARROW:
        case UPARROW:
        case DOWNARROW:
            editor.moveCursor(key);
            break;

        default:
            editor.insChar(key);
            break;
        }

        quitrepeat = QUIT_TIMES;
    }

    void setCursorPosition() {
        int cursorViewX = editor.cursor.x - offset.cols + 1;
        int cursorViewY = editor.cursor.y - offset.rows + 1;
        std::string cmd = "\x1b[" + std::to_string(cursorViewY) + ";" +
                          std::to_string(cursorViewX) + "H";
        buffer.append(cmd);
    }

    void drawScreen() {
        scroll();
        buffer.append(HIDECURSOR);
        buffer.append(RESETCURSOR);

        drawRows();
        drawStatusBar();
        drawMsgBar();
        setCursorPosition();

        buffer.append(SHOWCURSOR);

        write(STDOUT_FILENO, buffer.c_str(), static_cast<int>(buffer.size()));
        buffer.clear();
    }

    Interface(Editor &editor)
        : statusMsg(""), statusBorn(std::chrono::steady_clock::now()),
          editor(editor), offset{0, 0}, screen{0, 0} {
        if (tcgetattr(STDIN_FILENO, &originalTerminal) == -1)
            die("tcgettattr");
        getWindowSize();

        screen.rows -= SBARHEIGHT;

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
        interface.processKeypress();
    }

    return 0;
}
