#pragma once
#include "base.hpp"
#include "../string.hpp"

class Checkbox : public base_component
{
public:
    Checkbox(const stardustui::string& text, int width, int height);
    Checkbox(const stardustui::string& text, int width, int height, const SytelRules& style);
    ~Checkbox() override;

    void draw(unsigned long long handle) override;
    int get_preferred_width() const override;
    int get_preferred_height() const override;
    bool handle_left_button(bool pressed, int x, int y) override;

    void set_text(const stardustui::string& text);
    const stardustui::string& get_text() const;
    void set_checked(bool checked);
    bool is_checked() const;
    void set_toggle_callback(void (*func)(Checkbox&, bool));

private:
    stardustui::string text;
    bool checked;
    void (*toggle_callback_func)(Checkbox&, bool);

    bool set_checked_internal(bool checked, bool notify);
    int get_indicator_size(unsigned int text_size, unsigned int padding) const;
};
