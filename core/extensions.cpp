#include "editor.hpp"
#include "extensions.hpp"

std::string ExtensionHost::buffer() const {
    std::string context = {};
    for (int i = 0; i < editor.numlines(); i++) {
        if (i == editor.numlines() - 1) {
            context.append(editor.line_at(i).chars);
        } else {
            context.append(editor.line_at(i).chars);
            context.push_back('\n');
        }
    }

    return context;
}

void ExtensionHost::insert_text(std::string_view content) {
    if (editor.numlines() == 0)
        editor.insln(0, "");
    const int last_line = std::max(0, editor.numlines() - 1);
    editor.point(last_line, editor.line_at(last_line).size());

    for (char c : content) {
        if (c == '\r')
            continue;
        if (c == '\n')
            editor.insnewln_atptr();
        else
            editor.inschar(static_cast<echar>(c));
    }
}
