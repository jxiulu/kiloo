// tui

#pragma once

#include <chrono>
#include <fcntl.h>
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

class TUI;

class Action {
  public:
    virtual ~Action() = default;
    virtual void perform(Editor &app, TUI &ui) = 0;
};

class TUI {
  private:
    Terminal terminal;
    std::vector<std::unique_ptr<Extension>> extensions;
    std::optional<ExtensionHost> host;
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

    static constexpr int QUIT_TIMES = 2;

    int quit_repeat = QUIT_TIMES;

  public:
    static constexpr auto MSGLIF = std::chrono::seconds{5};
    static constexpr int SBARHEIGHT = 2;

    void register_extension(std::unique_ptr<Extension>);

    void update_index();
    int filled_rows();
    rowindex &row_at(int absy);
    int get_width(int row);
    int find_width(int row);

    int absy();
    int absy(int y);
    int get_charid();

    void move_cursor(echar key);
    void cursor_findloc(int lineid, int charid);
    void point_editor();

    void scroll();
    void print_welcomemsg();
    void draw_rows();
    void draw_statusbar();
    void draw_msgbar();
    void set_statusmsg(std::string);
    std::optional<std::string> prompt(std::string msgleft,
                                      std::optional<std::string> msgright);
    void draw_screen();

    void save();

    void quit();

    std::unique_ptr<Action> process_key(echar key);
    void receive_input();

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

class Quit final : public Action {
  private:
    int &rep;

  public:
    explicit Quit(int &quit_repeat) : rep(quit_repeat) {}
    void perform(Editor &e, TUI &ui) override {
        if (e.dirty() && rep > 0) {
            ui.set_statusmsg("File has unsaved changes. Press ^Q " +
                             std::to_string(rep) + " more times to quit.");
            rep--;
            return;
        }
        ui.quit();
    }
};

class Save final : public Action {
  public:
    void perform(Editor &, TUI &ui) override { ui.save(); };
};

class InsChar final : public Action {
  public:
    echar c;
    explicit InsChar(echar c) : c(c) {};
    void perform(Editor &e, TUI &) override { e.inschar(c); }
};

class MoveCursor final : public Action {
  public:
    echar key;
    explicit MoveCursor(echar k) : key(k) {};
    void perform(Editor &, TUI &ui) override { ui.move_cursor(key); }
};

class Return final : public Action {
  public:
    void perform(Editor &e, TUI &) override { e.insnewln_atptr(); }
};

class Delete final : public Action {
  public:
    echar key;
    explicit Delete(echar key) : key(key) {}
    void perform(Editor &e, TUI &ui) override {
        if (key == DEL) {
            ui.move_cursor(RIGHTARROW);
        }
        e.delchar();
    }
};

class Ignore final : public Action {
  public:
    void perform(Editor &, TUI &) override {};
};
