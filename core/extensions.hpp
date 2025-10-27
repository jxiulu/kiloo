#pragma once
#include <memory>
#include <string_view>

class Editor;
class TUI;

class ExtensionHost {
  private:
    Editor &editor;
    TUI &interface;

  public:
    ExtensionHost(Editor &editor, TUI &ui) : editor(editor), interface(ui) {}

    std::string buffer() const;

    void insert_text(std::string_view content);

    void set_statusmsg(std::string_view);

    Editor &core() { return editor; }
    TUI &ui() { return interface; }
};

class Extension {
  public:
    virtual ~Extension() = default;
    virtual void on_start(ExtensionHost &);
    virtual void on_key(int key, ExtensionHost &) = 0;
};
