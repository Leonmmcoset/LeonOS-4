#pragma once
#include "base.hpp"
#include "../string.hpp"

class RadioButton : public base_component
{
public:
    RadioButton(const stardustui::string& text, int width, int height);
    RadioButton(const stardustui::string& text, int width, int height, const SytelRules& style);
    ~RadioButton() override;

    void draw(unsigned long long handle) override;
    int get_preferred_width() const override;
    int get_preferred_height() const override;
    bool handle_left_button(bool pressed, int x, int y) override;

    void set_text(const stardustui::string& text);
    const stardustui::string& get_text() const;
    void set_group(const stardustui::string& group);
    const stardustui::string& get_group() const;
    void set_selected(bool selected);
    bool is_selected() const;
    void set_change_callback(void (*func)(RadioButton&, bool));

private:
    stardustui::string text;
    stardustui::string group;
    bool selected;
    void (*change_callback_func)(RadioButton&, bool);

    bool set_selected_internal(bool selected, bool notify);
    void clear_group_selection(bool notify);
    int get_indicator_size(unsigned int text_size, unsigned int padding) const;
};
