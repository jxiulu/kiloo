// terminal

#pragma once

#include <cstdio>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/ttycom.h>
#include <sys/types.h>
#include <termcap.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

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

#define ENTERALTBUF "\x1b[?1049h"
#define LEAVEALTBUF "\x1b[?1049l"
#define CLEARSCREEN "\x1b[2J" // size 4
#define RESETCURSOR "\x1b[H"  // size 3
#define WIPESCROLLBACK "\x1b[3J"
#define INVERTCOLOUR "\x1b[7m" // size 4
#define NORMALCOLOUR "\x1b[m"  // size 3
#define CLEARLINE "\x1b[K"     // size 3
#define HIDECURSOR "\x1b[?25l" // size 6
#define SHOWCURSOR "\x1b[?25h" // size 6

namespace {

class cursor_command {
  private:
    int x_;
    int y_;

  public:
    cursor_command(int x, int y) : x_(x), y_(y) {}
    int x() const { return x_; }
    int y() const { return y_; }
};

} // namespace

class Terminal {
  private:
    thing winsize;
    struct termios original;
    std::string out;

  public:
    Terminal &append(std::string_view content) {
        out.append(content);
        return *this;
    }

    Terminal &flush_buffer() {
        if (!out.empty()) {
            write(STDOUT_FILENO, out.data(), out.size());
            out.clear();
        }
        return *this;
    }

    Terminal &operator<<(Terminal &(*manip)(Terminal &)) {
        return manip(*this);
    }

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

template <void (Terminal::*member)()> class command {
  public:
    Terminal &operator()(Terminal &i) const {
        (i.*member)();
        return i;
    }
};
inline constexpr command<&Terminal::enable_raw> enable_raw{};
inline constexpr command<&Terminal::disable_raw> disable_raw{};
inline Terminal &operator<<(Terminal &i,
                            const command<&Terminal::enable_raw> &cmd) {
    return cmd(i);
}
inline Terminal &operator<<(Terminal &i,
                            const command<&Terminal::disable_raw> &cmd) {
    return cmd(i);
}

inline cursor_command place_cursor(int x, int y) {
    return cursor_command(x, y);
}
inline Terminal &operator<<(Terminal &term, const cursor_command &cmd) {
    return term.append("\x1b[" + std::to_string(cmd.y() + 1) + ";" +
                       std::to_string(cmd.x() + 1) + "H");
}

inline Terminal &enter_alternate_buffer(Terminal &t) {
    return t.append(ENTERALTBUF);
}
inline Terminal &leave_alternate_buffer(Terminal &t) {
    return t.append(LEAVEALTBUF);
}
inline Terminal &reset_cursor(Terminal &t) { return t.append(RESETCURSOR); }
inline Terminal &wipe_scrollback(Terminal &t) {
    return t.append(WIPESCROLLBACK);
}
inline Terminal &invcolour(Terminal &t) { return t.append(INVERTCOLOUR); }
inline Terminal &normcolour(Terminal &t) { return t.append(NORMALCOLOUR); }
inline Terminal &clearln(Terminal &t) { return t.append(CLEARLINE); }
inline Terminal &hide_cursor(Terminal &t) { return t.append(HIDECURSOR); }
inline Terminal &show_cursor(Terminal &t) { return t.append(SHOWCURSOR); }
inline Terminal &clear_screen(Terminal &t) { return t.append(CLEARSCREEN); }

inline Terminal &send(Terminal &t) { return t.flush_buffer(); }
