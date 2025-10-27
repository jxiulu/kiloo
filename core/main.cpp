#include "tui.hpp"

int main(int argc, char *argv[]) {
    Editor editor;
    Terminal terminal;
    TUI ui(editor, terminal);

    if (argc >= 2) {
        fs::path file(argv[1]);
        editor.open(file);
    }

    while (true) {
        ui.draw_screen();
        ui.process_key();
    }

    return 0;
}
