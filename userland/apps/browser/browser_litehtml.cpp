#include "browser_litehtml.h"

#include <litehtml.h>
#include <litehtml/render_item.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <leonos/png.h>
#include <leonos/fs.h>
#include <leonos/http.h>
#include <leonos/gui.h>
#include <leonos/psf_font.h>
#include <leonos/text.h>
#include <leonos/syscall.h>
#include <leonos/ui.h>
#ifdef __cplusplus
}
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

/* LeonOS form controls are rendered as LiteHTML replaced elements. */
namespace {

enum class form_kind : uint8_t {
    input_text,
    input_password,
    input_hidden,
    input_checkbox,
    input_radio,
    input_submit,
    input_reset,
    input_button,
    textarea,
    select,
    option,
    button,
};

static uint32_t form_px_value(litehtml::pixel_t value)
{
    float n = value.value();
    return n <= 0.0f ? 0U : (uint32_t)(n + 0.5f);
}

static int32_t form_signed_px(litehtml::pixel_t value)
{
    float n = value.value();
    return (int32_t)(n >= 0.0f ? n + 0.5f : n - 0.5f);
}

static void form_fill_rect(struct leonos_ui_surface *surface, int32_t x,
                           int32_t y, uint32_t width, uint32_t height,
                           uint32_t color,
                           const litehtml::position *clip = nullptr)
{
    if (!surface || !surface->pixels || !width || !height) {
        return;
    }
    int32_t left = std::max<int32_t>(x, 0);
    int32_t top = std::max<int32_t>(y, 0);
    int32_t right = std::min<int32_t>(x + (int32_t)width,
                                      (int32_t)surface->width);
    int32_t bottom = std::min<int32_t>(y + (int32_t)height,
                                       (int32_t)surface->height);
    if (clip) {
        left = std::max(left, form_signed_px(clip->x));
        top = std::max(top, form_signed_px(clip->y));
        right = std::min(right, form_signed_px(clip->right()));
        bottom = std::min(bottom, form_signed_px(clip->bottom()));
    }
    for (int32_t row = top; row < bottom; ++row) {
        uint32_t *dst = surface->pixels +
                        (uint64_t)(uint32_t)row * surface->stride +
                        (uint32_t)left;
        for (int32_t col = left; col < right; ++col) {
            *dst++ = color;
        }
    }
}

static bool form_ascii_equal(const char *left, const char *right)
{
    if (!left || !right) {
        return false;
    }
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return *left == 0 && *right == 0;
}

static uint32_t form_attribute_uint(const char *text, uint32_t fallback,
                                    uint32_t minimum, uint32_t maximum)
{
    uint64_t value = 0;
    if (!text || !*text) {
        return fallback;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10U + (uint32_t)(*text - '0');
        if (value >= maximum) {
            return maximum;
        }
        ++text;
    }
    if (*text || value < minimum) {
        return fallback;
    }
    return (uint32_t)value;
}

static form_kind form_kind_for(const char *tag, const char *type)
{
    if (form_ascii_equal(tag, "textarea")) {
        return form_kind::textarea;
    }
    if (form_ascii_equal(tag, "select")) {
        return form_kind::select;
    }
    if (form_ascii_equal(tag, "option")) {
        return form_kind::option;
    }
    if (form_ascii_equal(tag, "button")) {
        if (form_ascii_equal(type, "reset")) {
            return form_kind::input_reset;
        }
        if (form_ascii_equal(type, "submit")) {
            return form_kind::input_submit;
        }
        if (!type || !*type) {
            /* HTML defaults <button> to type=submit. */
            return form_kind::input_submit;
        }
        return form_kind::button;
    }
    if (form_ascii_equal(type, "password")) {
        return form_kind::input_password;
    }
    if (form_ascii_equal(type, "hidden")) {
        return form_kind::input_hidden;
    }
    if (form_ascii_equal(type, "checkbox")) {
        return form_kind::input_checkbox;
    }
    if (form_ascii_equal(type, "radio")) {
        return form_kind::input_radio;
    }
    if (form_ascii_equal(type, "submit")) {
        return form_kind::input_submit;
    }
    if (form_ascii_equal(type, "reset")) {
        return form_kind::input_reset;
    }
    if (form_ascii_equal(type, "button")) {
        return form_kind::input_button;
    }
    if (form_ascii_equal(type, "image")) {
        return form_kind::input_submit;
    }
    return form_kind::input_text;
}

class leonos_form_element final : public litehtml::html_tag {
  public:
    leonos_form_element(const std::shared_ptr<litehtml::document> &doc,
                        const char *tag) :
        litehtml::html_tag(doc), m_kind(form_kind_for(tag, nullptr))
    {
        /* Containers normally set this in their factory.  The custom form
         * factory owns the element creation, so preserve its real DOM tag for
         * CSS selectors, hit testing and form submission. */
        set_tagName(tag ? tag : "input");
    }

    void set_attr(const char *name, const char *value) override
    {
        litehtml::html_tag::set_attr(name, value);
        if (!name) {
            return;
        }
        if (form_ascii_equal(name, "type")) {
            /* Attribute iteration order is not guaranteed.  Re-evaluate from
             * the actual tag so <button type=...> is correct regardless of
             * whether type appears before or after value. */
            m_kind = form_kind_for(get_tagName(), value ? value : "");
        } else if (form_ascii_equal(name, "value")) {
            m_value = value ? value : "";
            m_initial_value = m_value;
            if (m_kind == form_kind::textarea) {
                m_text_initialized = true;
            }
        } else if (form_ascii_equal(name, "name")) {
            m_name = value ? value : "";
        } else if (form_ascii_equal(name, "placeholder")) {
            m_placeholder = value ? value : "";
        } else if (form_ascii_equal(name, "checked")) {
            m_checked = true;
            m_initial_checked = true;
        } else if (form_ascii_equal(name, "disabled")) {
            m_disabled = true;
        } else if (form_ascii_equal(name, "readonly")) {
            m_readonly = true;
        } else if (form_ascii_equal(name, "selected")) {
            m_selected = true;
        } else if (form_ascii_equal(name, "multiple")) {
            m_multiple = true;
        } else if (form_ascii_equal(name, "maxlength")) {
            m_max_length = form_attribute_uint(value, 0U, 0U, 4096U);
        }
    }

    bool is_replaced() const override
    {
        return m_kind != form_kind::option && m_kind != form_kind::input_hidden;
    }

    void get_content_size(litehtml::size &size, litehtml::pixel_t max_width) override
    {
        uint32_t width = 190U;
        uint32_t height = 26U;
        if (m_kind == form_kind::input_hidden) {
            size.width = litehtml::pixel_t(0);
            size.height = litehtml::pixel_t(0);
            return;
        }
        if (m_kind == form_kind::input_checkbox ||
            m_kind == form_kind::input_radio) {
            width = 18U;
            height = 18U;
        } else if (m_kind == form_kind::textarea) {
            width = form_attribute_uint(get_attr("cols", nullptr), 32U,
                                        1U, 160U) * LEONOS_FONT_W + 10U;
            height = form_attribute_uint(get_attr("rows", nullptr), 4U,
                                         1U, 32U) * LEONOS_FONT_H + 10U;
        } else if (m_kind == form_kind::select) {
            width = 18U * LEONOS_FONT_W + 26U;
            height = select_rows() * LEONOS_FONT_H + 10U;
        } else if (m_kind == form_kind::input_submit ||
                   m_kind == form_kind::input_reset ||
                   m_kind == form_kind::input_button ||
                   m_kind == form_kind::button) {
            width = 112U;
        } else if (m_kind == form_kind::input_text ||
                   m_kind == form_kind::input_password) {
            width = form_attribute_uint(get_attr("size", nullptr), 20U,
                                        1U, 160U) * LEONOS_FONT_W + 10U;
        }
        if (max_width.value() > 0.0f && max_width.value() < (float)width) {
            width = (uint32_t)max_width.value();
        }
        size.width = litehtml::pixel_t((int)width);
        size.height = litehtml::pixel_t((int)height);
    }

    void draw(litehtml::uint_ptr hdc, litehtml::pixel_t x,
              litehtml::pixel_t y, const litehtml::position *clip,
              const std::shared_ptr<litehtml::render_item> &ri) override
    {
        struct leonos_ui_surface *surface =
            (struct leonos_ui_surface *)(uintptr_t)hdc;
        litehtml::position pos = ri ? ri->pos() : litehtml::position();
        int32_t left = form_signed_px(pos.x) + form_signed_px(x);
        int32_t top = form_signed_px(pos.y) + form_signed_px(y);
        uint32_t width = form_px_value(pos.width);
        uint32_t height = form_px_value(pos.height);
        uint32_t bg = m_disabled ? 0x00e0e0e0U : 0x00ffffffU;
        uint32_t fg = m_disabled ? 0x00787878U : 0x00181818U;
        (void)clip;
        if (!surface || !width || !height) {
            return;
        }
        form_fill_rect(surface, left, top, width, height, bg, clip);
        form_fill_rect(surface, left, top, width, 1U, 0x00708080U, clip);
        form_fill_rect(surface, left, top + (int32_t)height - 1, width, 1U, 0x00708080U, clip);
        form_fill_rect(surface, left, top, 1U, height, 0x00708080U, clip);
        form_fill_rect(surface, left + (int32_t)width - 1, top, 1U, height, 0x00708080U, clip);
        if (m_kind == form_kind::input_checkbox || m_kind == form_kind::input_radio) {
            if (m_checked) {
                form_fill_rect(surface, left + 4, top + 4, width > 8U ? width - 8U : 1U,
                          height > 8U ? height - 8U : 1U, 0x002f65c8U, clip);
            }
            if (m_focused) {
                form_fill_rect(surface, left + 1, top + 1, width > 2U ? width - 2U : 1U,
                               1U, 0x002f65c8U, clip);
            }
            return;
        }
        initialize_textarea();
        std::string text = m_value;
        if (m_kind == form_kind::select) {
            choose_initial_option();
            text = selected_option_label();
        } else if (text.empty() && (m_kind == form_kind::button ||
                                    m_kind == form_kind::input_button ||
                                    m_kind == form_kind::input_submit ||
                                    m_kind == form_kind::input_reset)) {
            get_text(text);
        }
        if (text.empty()) {
            text = m_placeholder;
        }
        if (text.empty()) {
            if (m_kind == form_kind::input_submit) {
                text = "Submit";
            } else if (m_kind == form_kind::input_reset) {
                text = "Reset";
            } else if (m_kind == form_kind::input_button ||
                       m_kind == form_kind::button) {
                text = "Button";
            }
        }
        if (m_kind == form_kind::input_password) {
            text.assign(m_value.size(), '*');
        }
        if (m_kind == form_kind::textarea && !text.empty() && width > 8U && height > 8U) {
            uint32_t line_height = std::min<uint32_t>(LEONOS_FONT_H, height - 8U);
            uint32_t text_y = 0;
            std::size_t start = 0;
            while (start <= text.size() && text_y + line_height <= height - 8U) {
                std::size_t end = text.find('\n', start);
                std::string line = text.substr(start, end == std::string::npos ?
                                                         std::string::npos : end - start);
                leonos_ui_text_scaled_transparent_clipped_at(
                    surface, left + 4, top + 4 + (int32_t)text_y, width - 8U,
                    line.c_str(), fg, line_height);
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1U;
                text_y += line_height;
            }
        } else if (m_kind != form_kind::select && !text.empty() &&
                   width > 8U && height > 8U) {
            uint32_t text_height = std::min<uint32_t>(height - 8U,
                                                       LEONOS_FONT_H);
            leonos_ui_text_scaled_transparent_clipped_at(
                surface, left + 4, top + 4, width - 8U, text.c_str(), fg,
                text_height);
        }
        if (m_kind == form_kind::select) {
            uint32_t rows = select_rows();
            std::vector<litehtml::element::ptr> options;
            option_elements(options);
            form_fill_rect(surface, left + 1, top + 1,
                           width > 2U ? width - 2U : 1U,
                           height > 2U ? height - 2U : 1U,
                           0x00ffffffU, clip);
            if (rows == 1U) {
                /* A collapsed select displays the selected option, not
                 * necessarily the first option in the DOM. */
                std::string label = selected_option_label();
                leonos_ui_text_scaled_transparent_clipped_at(
                    surface, left + 4, top + 4,
                    width > 8U ? width - 8U : 1U, label.c_str(), fg,
                    LEONOS_FONT_H);
            }
            for (uint32_t row = 0; rows > 1U && row < rows && row < options.size(); ++row) {
                std::string label;
                options[row]->get_text(label);
                std::string value = option_value_for(options[row]);
                bool selected = selected_value(value);
                int32_t row_y = top + 4 + (int32_t)(row * LEONOS_FONT_H);
                if (selected) {
                    form_fill_rect(surface, left + 2, row_y - 2,
                                   width > 4U ? width - 4U : 1U,
                                   LEONOS_FONT_H, 0x002f65c8U, clip);
                }
                leonos_ui_text_scaled_transparent_clipped_at(
                    surface, left + 4, row_y,
                    width > 8U ? width - 8U : 1U, label.c_str(),
                    selected ? 0x00ffffffU : fg, LEONOS_FONT_H);
            }
            if (rows == 1U) {
                form_fill_rect(surface, left + (int32_t)width - 18, top + 1, 17U,
                          height > 2U ? height - 2U : 1U, 0x00d8e8f8U, clip);
                form_fill_rect(surface, left + (int32_t)width - 12, top + 10, 6U, 2U,
                          0x00304050U, clip);
            }
        }
        if (m_focused) {
            form_fill_rect(surface, left + 1, top + 1, width > 2U ? width - 2U : 1U,
                           1U, 0x002f65c8U, clip);
            form_fill_rect(surface, left + 1, top + (int32_t)height - 2,
                           width > 2U ? width - 2U : 1U, 1U, 0x002f65c8U, clip);
        }
    }

    form_kind kind() const { return m_kind; }
    bool disabled() const { return m_disabled; }
    bool readonly() const { return m_readonly; }
    bool checked() const { return m_checked; }
    void checked(bool value) { m_checked = value; }
    void focused(bool value) { m_focused = value; }
    const std::string &value() const { return m_value; }
    void value(const std::string &value) { m_value = value; }
    const std::string &submit_value() const
    {
        static const std::string default_checkbox_value("on");
        if ((m_kind == form_kind::input_checkbox ||
             m_kind == form_kind::input_radio) && m_value.empty()) {
            return default_checkbox_value;
        }
        return m_value;
    }
    const std::string &name() const { return m_name; }
    uint32_t max_length() const { return m_max_length; }
    bool editable() const
    {
        return m_kind == form_kind::input_text ||
               m_kind == form_kind::input_password ||
               m_kind == form_kind::textarea;
    }
    bool focusable() const
    {
        return !m_disabled && m_kind != form_kind::input_hidden &&
               m_kind != form_kind::option;
    }
    bool selected() const { return m_selected; }
    bool multiple() const { return m_multiple; }
    uint32_t select_rows() const
    {
        if (m_kind != form_kind::select) {
            return 1U;
        }
        const char *size_attr = get_attr("size", nullptr);
        return size_attr ? form_attribute_uint(size_attr, 1U, 1U, 32U)
                         : (m_multiple ? 4U : 1U);
    }
    bool selected_value(const std::string &value)
    {
        choose_initial_option();
        return m_multiple
                   ? std::find(m_selected_values.begin(), m_selected_values.end(),
                               value) != m_selected_values.end()
                   : value == m_value;
    }
    const std::string &initial_value() const { return m_initial_value; }
    bool initial_checked() const { return m_initial_checked; }
    void initialize_textarea_value()
    {
        initialize_textarea();
    }
    void reset_value()
    {
        initialize_textarea();
        m_value = m_initial_value;
        m_checked = m_initial_checked;
    }

    void reset_select()
    {
        if (m_kind == form_kind::select) {
            m_value.clear();
            m_selected_values.clear();
            m_select_initialized = false;
            choose_initial_option();
        }
    }

    std::vector<std::string> selected_values()
    {
        std::vector<std::string> values;
        if (m_kind != form_kind::select) {
            return values;
        }
        choose_initial_option();
        if (m_multiple) {
            values = m_selected_values;
        } else {
            std::vector<litehtml::element::ptr> options;
            option_elements(options);
            if (m_select_initialized && !options.empty()) {
                /* value="" is a valid successful-control value. */
                values.push_back(m_value);
            }
        }
        return values;
    }

    void toggle_selected_option()
    {
        if (m_kind != form_kind::select) {
            return;
        }
        choose_initial_option();
        if (!m_multiple) {
            return;
        }
        std::vector<std::string>::iterator it = std::find(
            m_selected_values.begin(), m_selected_values.end(), m_value);
        if (it == m_selected_values.end()) {
            m_selected_values.push_back(m_value);
        } else {
            m_selected_values.erase(it);
        }
    }

    std::string selected_option_label()
    {
        if (m_kind != form_kind::select) {
            return {};
        }
        std::vector<litehtml::element::ptr> options;
        option_elements(options);
        for (const auto &child : options) {
            std::string option_value = option_value_for(child);
            if (option_value == m_value) {
                std::string label;
                child->get_text(label);
                return label.empty() ? option_value : label;
            }
        }
        return m_value;
    }

    void choose_initial_option()
    {
        if (m_kind != form_kind::select || m_select_initialized) {
            return;
        }
        bool found_selected = false;
        std::vector<litehtml::element::ptr> options;
        option_elements(options);
        for (const auto &child : options) {
            if (option_enabled(child) && child->get_attr("selected", nullptr)) {
                std::string value = option_value_for(child);
                if (m_multiple) {
                    m_selected_values.push_back(value);
                } else if (!found_selected) {
                    m_value = value;
                    found_selected = true;
                }
            }
        }
        if (m_multiple) {
            if (m_selected_values.empty()) {
                for (const auto &child : options) {
                    if (option_enabled(child)) {
                        m_selected_values.push_back(option_value_for(child));
                        break;
                    }
                }
            }
            if (m_value.empty() && !m_selected_values.empty()) {
                m_value = m_selected_values.front();
            }
        } else if (!found_selected) {
            for (const auto &child : options) {
                if (option_enabled(child)) {
                    m_value = option_value_for(child);
                    break;
                }
            }
        }
        m_select_initialized = true;
    }

    void choose_next_option()
    {
        if (m_kind != form_kind::select) {
            return;
        }
        std::vector<litehtml::element::ptr> options;
        option_elements(options);
        if (options.empty()) {
            return;
        }
        std::size_t index = 0;
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (option_value_for(options[i]) == m_value) {
                index = i;
                break;
            }
        }
        for (std::size_t offset = 1; offset <= options.size(); ++offset) {
            const std::size_t candidate = (index + offset) % options.size();
            if (option_enabled(options[candidate])) {
                m_value = option_value_for(options[candidate]);
                return;
            }
        }
    }

    void choose_previous_option()
    {
        if (m_kind != form_kind::select) {
            return;
        }
        std::vector<litehtml::element::ptr> options;
        option_elements(options);
        if (options.empty()) {
            return;
        }
        std::size_t index = 0;
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (option_value_for(options[i]) == m_value) {
                index = i;
                break;
            }
        }
        for (std::size_t offset = 1; offset <= options.size(); ++offset) {
            const std::size_t candidate =
                (index + options.size() - offset) % options.size();
            if (option_enabled(options[candidate])) {
                m_value = option_value_for(options[candidate]);
                return;
            }
        }
    }

    void choose_option_at(int32_t y)
    {
        if (m_kind != form_kind::select) {
            return;
        }
        choose_initial_option();
        std::vector<litehtml::element::ptr> options;
        option_elements(options);
        if (options.empty()) {
            return;
        }
        if (select_rows() == 1U) {
            choose_next_option();
            return;
        }
        litehtml::position placement = get_placement();
        int32_t local_y = y - form_signed_px(placement.y) - 4;
        if (local_y < 0) {
            return;
        }
        std::size_t index = (std::size_t)local_y / LEONOS_FONT_H;
        if (index >= options.size()) {
            return;
        }
        if (!option_enabled(options[index])) {
            return;
        }
        std::string value = option_value_for(options[index]);
        if (m_multiple) {
            std::vector<std::string>::iterator it = std::find(
                m_selected_values.begin(), m_selected_values.end(), value);
            if (it == m_selected_values.end()) {
                m_selected_values.push_back(value);
            } else {
                m_selected_values.erase(it);
            }
            m_value = value;
        } else {
            m_value = value;
        }
    }

    std::shared_ptr<litehtml::render_item> create_render_item(
        const std::shared_ptr<litehtml::render_item> &parent_ri) override
    {
        /* Options are metadata for their owning select, and hidden inputs
         * participate in submission only.  Rendering either would leak their
         * text into the page outside of the native-looking control. */
        if (m_kind == form_kind::option || m_kind == form_kind::input_hidden) {
            return {};
        }
        return litehtml::html_tag::create_render_item(parent_ri);
    }

  private:
    static std::string option_value_for(const litehtml::element::ptr &option)
    {
        if (!option) {
            return {};
        }
        const char *value = option->get_attr("value", nullptr);
        if (value) {
            return value;
        }
        std::string text;
        option->get_text(text);
        return text;
    }

    static bool option_enabled(const litehtml::element::ptr &option)
    {
        return option && !option->get_attr("disabled", nullptr);
    }

    static void append_option_elements(
        const litehtml::element::ptr &element,
        std::vector<litehtml::element::ptr> &out)
    {
        if (!element) {
            return;
        }
        if (form_ascii_equal(element->get_tagName(), "option")) {
            out.push_back(element);
            return;
        }
        for (const auto &child : element->children()) {
            append_option_elements(child, out);
        }
    }

    void option_elements(std::vector<litehtml::element::ptr> &out)
    {
        for (const auto &child : children()) {
            append_option_elements(child, out);
        }
    }

    form_kind m_kind;
    bool m_checked = false;
    bool m_disabled = false;
    bool m_readonly = false;
    bool m_initial_checked = false;
    bool m_selected = false;
    bool m_multiple = false;
    bool m_focused = false;
    bool m_select_initialized = false;
    uint32_t m_max_length = 0;
    std::string m_name;
    std::string m_value;
    std::string m_initial_value;
    std::string m_placeholder;
    std::vector<std::string> m_selected_values;
    bool m_text_initialized = false;

    void initialize_textarea()
    {
        if (m_kind != form_kind::textarea || m_text_initialized) {
            return;
        }
        get_text(m_value);
        m_initial_value = m_value;
        m_text_initialized = true;
    }
};

struct litehtml_font {
    uint32_t size;
    uint32_t weight;
};

static uint32_t px_value(litehtml::pixel_t value)
{
    float n = value.value();
    if (n <= 0.0f) {
        return 0;
    }
    if (n >= 4294967295.0f) {
        return 0xffffffffU;
    }
    return (uint32_t)(n + 0.5f);
}

static int32_t signed_px(litehtml::pixel_t value)
{
    float n = value.value();
    if (n <= -2147483648.0f) {
        return (-2147483647 - 1);
    }
    if (n >= 2147483647.0f) {
        return 2147483647;
    }
    return (int32_t)(n >= 0.0f ? n + 0.5f : n - 0.5f);
}

static uint32_t color_value(const litehtml::web_color &color)
{
    return ((uint32_t)color.red << 16) |
           ((uint32_t)color.green << 8) |
           (uint32_t)color.blue;
}

static uint32_t blend_color(uint32_t background, uint32_t foreground,
                            uint8_t alpha)
{
    uint32_t inv = 255U - alpha;
    uint32_t r = (((background >> 16) & 0xffU) * inv +
                  ((foreground >> 16) & 0xffU) * alpha + 127U) / 255U;
    uint32_t g = (((background >> 8) & 0xffU) * inv +
                  ((foreground >> 8) & 0xffU) * alpha + 127U) / 255U;
    uint32_t b = ((background & 0xffU) * inv +
                  (foreground & 0xffU) * alpha + 127U) / 255U;
    return (r << 16) | (g << 8) | b;
}

static bool rect_visible(const leonos_ui_surface *surface,
                         const litehtml::position &pos)
{
    if (!surface) {
        return false;
    }
    return signed_px(pos.right()) > 0 && signed_px(pos.bottom()) > 0 &&
           signed_px(pos.x) < (int32_t)surface->width &&
           signed_px(pos.y) < (int32_t)surface->height;
}

static std::string local_path(const std::string &url,
                              const std::string &base_url)
{
    std::string path = url;
    std::string prefix;
    std::vector<std::string> parts;
    std::string result;
    std::string::size_type query = path.find_first_of("?#");
    if (query != std::string::npos) {
        path.erase(query);
    }
    if (path.compare(0, 3, "0:/") == 0) {
        prefix = "0:/";
        path.erase(0, 3);
    } else if (path.compare(0, 1, "/") == 0) {
        /* A leading slash on an HTTP document is origin-relative, not a
         * path on the LeonOS root filesystem. */
        if (base_url.compare(0, 3, "0:/") != 0) {
            return {};
        }
        prefix = "0:/";
        path.erase(0, 1);
    } else if (base_url.compare(0, 3, "0:/") == 0) {
        std::string::size_type slash = base_url.find_last_of('/');
        prefix = slash == std::string::npos ? "0:/" : base_url.substr(0, slash + 1U);
        if (prefix.compare(0, 3, "0:/") == 0) {
            path = prefix.substr(3) + path;
            prefix = "0:/";
        }
    } else {
        return {};
    }
    std::string::size_type start = 0;
    while (start <= path.size()) {
        std::string::size_type slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ?
                                               std::string::npos : slash - start);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                if (!parts.empty()) {
                    parts.pop_back();
                }
            } else {
                parts.push_back(part);
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1U;
    }
    result = prefix;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (result.size() > 3U && result[result.size() - 1U] != '/') {
            result.push_back('/');
        }
        result += parts[i];
    }
    return result;
}

static bool ascii_prefix_ci(const std::string &value, const char *prefix)
{
    if (!prefix) {
        return false;
    }
    for (std::size_t i = 0; prefix[i]; ++i) {
        if (i >= value.size()) {
            return false;
        }
        char a = value[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static bool is_http_url(const std::string &url)
{
    return ascii_prefix_ci(url, "http://") ||
           ascii_prefix_ci(url, "https://");
}

static std::string remote_url(const std::string &url,
                              const std::string &base_url)
{
    char resolved[LEONOS_HTTP_URL_LEN];
    if (is_http_url(url)) {
        return url;
    }
    if (url.compare(0, 2, "//") == 0) {
        if (ascii_prefix_ci(base_url, "https://")) {
            return std::string("https:") + url;
        }
        if (ascii_prefix_ci(base_url, "http://")) {
            return std::string("http:") + url;
        }
    }
    if (!is_http_url(base_url) ||
        leonos_http_resolve_url(base_url.c_str(), url.c_str(), resolved,
                                sizeof(resolved)) < 0) {
        return {};
    }
    return resolved;
}

static bool read_text_file(const std::string &path, std::string &out)
{
    struct leonos_stat st;
    const uint32_t max_size = 128U * 1024U;
    int fd;
    if (path.empty() || stat(path.c_str(), &st) < 0 ||
        st.type != LEONOS_FS_TYPE_FILE || st.size > max_size) {
        return false;
    }
    fd = open(path.c_str(), LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }
    out.assign((std::size_t)st.size, '\0');
    std::size_t offset = 0;
    while (offset < out.size()) {
        long got = read(fd, &out[offset], out.size() - offset);
        if (got <= 0) {
            close(fd);
            out.clear();
            return false;
        }
        offset += (std::size_t)got;
    }
    close(fd);
    return true;
}

static bool decode_document_text(const char *data, std::size_t length,
                                 std::string &out)
{
    uint32_t encoding = LEONOS_TEXT_ENCODING_UTF8;
    uint32_t decoded_len = 0;
    uint32_t replacements = 0;
    std::size_t capacity;
    std::vector<char> decoded;
    if ((!data && length) || leonos_text_detect_encoding(
                                data, (uint32_t)length, &encoding) < 0) {
        return false;
    }
    if (length > (std::size_t)0xffffffffU / 3U - 4U) {
        return false;
    }
    capacity = length * 3U + 4U;
    decoded.resize(capacity);
    if (leonos_text_decode(data, (uint32_t)length, encoding, decoded.data(),
                           (uint32_t)decoded.size(), &decoded_len,
                           &replacements) < 0) {
        return false;
    }
    out.assign(decoded.data(), (std::size_t)decoded_len);
    (void)replacements;
    return true;
}

class leonos_container final : public litehtml::document_container {
  public:
    leonos_container(browser_litehtml_link_callback link_callback,
                     browser_litehtml_title_callback title_callback,
                     browser_litehtml_submit_callback submit_callback,
                     browser_litehtml_resource_callback resource_callback,
                     void *opaque) :
        m_link_callback(link_callback),
        m_title_callback(title_callback),
        m_submit_callback(submit_callback),
        m_resource_callback(resource_callback),
        m_opaque(opaque)
    {
    }

    ~leonos_container() override
    {
        release_images();
    }

    litehtml::uint_ptr create_font(const litehtml::font_description &descr,
                                   const litehtml::document *,
                                   litehtml::font_metrics *metrics) override
    {
        litehtml_font *font = (litehtml_font *)std::malloc(sizeof(*font));
        uint32_t size = px_value(descr.size);
        if (!size) {
            size = 16U;
        }
        if (!font) {
            return 0;
        }
        font->size = size;
        font->weight = (uint32_t)descr.weight;
        if (metrics) {
            metrics->font_size = litehtml::pixel_t((int)size);
            metrics->height = litehtml::pixel_t((int)size);
            metrics->ascent = litehtml::pixel_t((int)((size * 4U) / 5U));
            metrics->descent = litehtml::pixel_t((int)(size - (size * 4U) / 5U));
            metrics->x_height = litehtml::pixel_t((int)((size * 1U) / 2U));
            metrics->ch_width = litehtml::pixel_t((int)((size * 1U) / 2U));
            metrics->draw_spaces = true;
            metrics->sub_shift = litehtml::pixel_t(0);
            metrics->super_shift = litehtml::pixel_t(0);
        }
        return (litehtml::uint_ptr)font;
    }

    void delete_font(litehtml::uint_ptr handle) override
    {
        std::free((void *)(uintptr_t)handle);
    }

    litehtml::pixel_t text_width(const char *text,
                                 litehtml::uint_ptr handle) override
    {
        const litehtml_font *font = (const litehtml_font *)(uintptr_t)handle;
        uint32_t size = font && font->size ? font->size : 16U;
        uint32_t width = leonos_ui_text_scaled_width(text ? text : "", size);
        return litehtml::pixel_t((int)width);
    }

    void draw_text(litehtml::uint_ptr hdc, const char *text,
                   litehtml::uint_ptr handle, litehtml::web_color color,
                   const litehtml::position &pos) override
    {
        struct leonos_ui_surface *surface =
            (struct leonos_ui_surface *)(uintptr_t)hdc;
        if (!surface || !text || !rect_visible(surface, pos) ||
            !box_intersects_clips(pos.x.value(), pos.y.value(),
                                  pos.width.value(), pos.height.value())) {
            return;
        }
        const litehtml_font *font =
            (const litehtml_font *)(uintptr_t)handle;
        uint32_t size = font && font->size ? font->size : 16U;
        int32_t x = signed_px(pos.x);
        int32_t y = signed_px(pos.y);
        uint32_t width = px_value(pos.width);
        if (!width) {
            width = text_width(text, handle).value();
        }
        leonos_ui_text_scaled_transparent_clipped_at(
            surface, x, y, width, text, color_value(color), size);
    }

    litehtml::pixel_t pt_to_px(float pt) const override
    {
        return (litehtml::pixel_t)(pt * 96.0f / 72.0f);
    }

    litehtml::pixel_t get_default_font_size() const override
    {
        return litehtml::pixel_t(16);
    }

    const char *get_default_font_name() const override
    {
        return "Times New Roman";
    }

    void draw_list_marker(litehtml::uint_ptr hdc,
                          const litehtml::list_marker &marker) override
    {
        struct leonos_ui_surface *surface =
            (struct leonos_ui_surface *)(uintptr_t)hdc;
        if (!surface || !rect_visible(surface, marker.pos)) {
            return;
        }
        uint32_t color = color_value(marker.color);
        int32_t x = signed_px(marker.pos.x);
        int32_t y = signed_px(marker.pos.y);
        uint32_t w = std::max<uint32_t>(2U, px_value(marker.pos.width));
        uint32_t h = std::max<uint32_t>(2U, px_value(marker.pos.height));
        fill_clipped(surface, x, y, w, h, color);
    }

    void load_image(const char *src, const char *baseurl, bool) override
    {
        (void)image_for_url(src ? src : "", baseurl ? baseurl : "");
    }

    void get_image_size(const char *src, const char *baseurl,
                        litehtml::size &size) override
    {
        size = litehtml::size();
        image_entry *image = image_for_url(src ? src : "",
                                           baseurl ? baseurl : "");
        if (image) {
            size.width = litehtml::pixel_t((int)image->width);
            size.height = litehtml::pixel_t((int)image->height);
        }
    }

    void draw_image(litehtml::uint_ptr hdc,
                    const litehtml::background_layer &layer,
                    const std::string &url,
                    const std::string &base_url) override
    {
        struct leonos_ui_surface *surface =
            (struct leonos_ui_surface *)(uintptr_t)hdc;
        if (!surface || !rect_visible(surface, layer.border_box)) {
            return;
        }
        image_entry *image = image_for_url(url, base_url);
        if (image) {
            uint32_t image_w = image->width;
            uint32_t image_h = image->height;
            int32_t origin_x = signed_px(layer.origin_box.x);
            int32_t origin_y = signed_px(layer.origin_box.y);
            uint32_t tile_w = px_value(layer.origin_box.width);
            uint32_t tile_h = px_value(layer.origin_box.height);
            int32_t clip_left = std::max<int32_t>(signed_px(layer.clip_box.x), 0);
            int32_t clip_top = std::max<int32_t>(signed_px(layer.clip_box.y), 0);
            int32_t clip_right = std::min<int32_t>(signed_px(layer.clip_box.right()),
                                                   (int32_t)surface->width);
            int32_t clip_bottom = std::min<int32_t>(signed_px(layer.clip_box.bottom()),
                                                    (int32_t)surface->height);
            bool repeat_x = layer.repeat == litehtml::background_repeat_repeat ||
                            layer.repeat == litehtml::background_repeat_repeat_x;
            bool repeat_y = layer.repeat == litehtml::background_repeat_repeat ||
                            layer.repeat == litehtml::background_repeat_repeat_y;
            if (!tile_w || !tile_h || !image_w || !image_h ||
                clip_right <= clip_left || clip_bottom <= clip_top) {
                return;
            }
            for (int32_t py = clip_top; py < clip_bottom; ++py) {
                int32_t local_y = py - origin_y;
                if (!repeat_y && (local_y < 0 || local_y >= (int32_t)tile_h)) {
                    continue;
                }
                if (repeat_y) {
                    local_y %= (int32_t)tile_h;
                    if (local_y < 0) {
                        local_y += (int32_t)tile_h;
                    }
                }
                uint32_t sy = (uint32_t)(((uint64_t)local_y * image_h) / tile_h);
                if (sy >= image_h) {
                    sy = image_h - 1U;
                }
                for (int32_t px = clip_left; px < clip_right; ++px) {
                    int32_t local_x = px - origin_x;
                    if (!repeat_x && (local_x < 0 || local_x >= (int32_t)tile_w)) {
                        continue;
                    }
                    if (repeat_x) {
                        local_x %= (int32_t)tile_w;
                        if (local_x < 0) {
                            local_x += (int32_t)tile_w;
                        }
                    }
                    uint32_t sx = (uint32_t)(((uint64_t)local_x * image_w) / tile_w);
                    if (sx >= image_w) {
                        sx = image_w - 1U;
                    }
                    if (point_visible(px, py)) {
                        surface->pixels[(uint64_t)(uint32_t)py * surface->stride +
                                        (uint32_t)px] =
                            image->pixels[(uint64_t)sy * image_w + sx];
                    }
                }
            }
            return;
        }
        fill_clipped(surface, signed_px(layer.clip_box.x),
                  signed_px(layer.clip_box.y), px_value(layer.clip_box.width),
                  px_value(layer.clip_box.height), 0x00eef2f6U);
    }

    void draw_solid_fill(litehtml::uint_ptr hdc,
                         const litehtml::background_layer &layer,
                         const litehtml::web_color &color) override
    {
        struct leonos_ui_surface *surface =
            (struct leonos_ui_surface *)(uintptr_t)hdc;
        if (!surface || !rect_visible(surface, layer.clip_box)) {
            return;
        }
        fill_clipped(surface, signed_px(layer.clip_box.x), signed_px(layer.clip_box.y),
                  px_value(layer.clip_box.width), px_value(layer.clip_box.height),
                  color_value(color), color.alpha);
    }

    void draw_linear_gradient(litehtml::uint_ptr hdc,
                              const litehtml::background_layer &layer,
                              const litehtml::background_layer::linear_gradient &gradient) override
    {
        struct leonos_ui_surface *surface =
            (struct leonos_ui_surface *)(uintptr_t)hdc;
        if (!surface || gradient.color_points.empty()) {
            return;
        }
        draw_gradient(surface, layer, gradient.color_points,
                      [gradient](float px, float py) {
                          float dx = gradient.end.x - gradient.start.x;
                          float dy = gradient.end.y - gradient.start.y;
                          float length = dx * dx + dy * dy;
                          return length > 0.0001f
                                     ? ((px - gradient.start.x) * dx +
                                        (py - gradient.start.y) * dy) / length
                                     : 0.0f;
                      });
    }

    void draw_radial_gradient(litehtml::uint_ptr hdc,
                              const litehtml::background_layer &layer,
                              const litehtml::background_layer::radial_gradient &gradient) override
    {
        struct leonos_ui_surface *surface =
            (struct leonos_ui_surface *)(uintptr_t)hdc;
        if (!surface || gradient.color_points.empty()) {
            return;
        }
        draw_gradient(surface, layer, gradient.color_points,
                      [gradient](float px, float py) {
                          float rx = gradient.radius.x;
                          float ry = gradient.radius.y;
                          float dx = px - gradient.position.x;
                          float dy = py - gradient.position.y;
                          if (rx <= 0.0001f || ry <= 0.0001f) {
                              return 0.0f;
                          }
                          return std::sqrt((dx * dx) / (rx * rx) +
                                           (dy * dy) / (ry * ry));
                      });
    }

    void draw_conic_gradient(litehtml::uint_ptr hdc,
                             const litehtml::background_layer &layer,
                             const litehtml::background_layer::conic_gradient &gradient) override
    {
        struct leonos_ui_surface *surface =
            (struct leonos_ui_surface *)(uintptr_t)hdc;
        if (!surface || gradient.color_points.empty()) {
            return;
        }
        draw_gradient(surface, layer, gradient.color_points,
                      [gradient](float px, float py) {
                          float angle = std::atan2(py - gradient.position.y,
                                                  px - gradient.position.x) -
                                        gradient.angle * 3.1415926535f / 180.0f;
                          float turn = angle / (2.0f * 3.1415926535f);
                          turn -= std::floor(turn);
                          return turn;
                      });
    }

    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders &borders,
                      const litehtml::position &pos, bool) override
    {
        struct leonos_ui_surface *surface =
            (struct leonos_ui_surface *)(uintptr_t)hdc;
        if (!surface) {
            return;
        }
        uint32_t x = px_value(pos.x);
        uint32_t y = px_value(pos.y);
        uint32_t w = px_value(pos.width);
        uint32_t h = px_value(pos.height);
        uint32_t left_w = std::min<uint32_t>(px_value(borders.left.width), w);
        uint32_t right_w = std::min<uint32_t>(px_value(borders.right.width), w);
        uint32_t top_h = std::min<uint32_t>(px_value(borders.top.width), h);
        uint32_t bottom_h = std::min<uint32_t>(px_value(borders.bottom.width), h);
        if (left_w) {
            fill_clipped(surface, (int32_t)x, (int32_t)y, left_w, h,
                      color_value(borders.left.color), borders.left.color.alpha);
        }
        if (top_h) {
            fill_clipped(surface, (int32_t)x, (int32_t)y, w, top_h,
                      color_value(borders.top.color), borders.top.color.alpha);
        }
        if (right_w) {
            fill_clipped(surface, (int32_t)(x + w - right_w), (int32_t)y,
                      right_w, h, color_value(borders.right.color),
                      borders.right.color.alpha);
        }
        if (bottom_h) {
            fill_clipped(surface, (int32_t)x, (int32_t)(y + h - bottom_h),
                      w, bottom_h, color_value(borders.bottom.color),
                      borders.bottom.color.alpha);
        }
    }

    void set_caption(const char *caption) override
    {
        if (m_title_callback) {
            m_title_callback(m_opaque, caption ? caption : "");
        }
    }

    void set_base_url(const char *base_url) override
    {
        m_base_url = base_url ? base_url : "";
    }

    void link(const std::shared_ptr<litehtml::document> &, const litehtml::element::ptr &) override
    {
    }

    void on_anchor_click(const char *url, const litehtml::element::ptr &) override
    {
        if (m_link_callback) {
            std::string target = url ? url : "";
            std::string local = local_path(target, m_base_url);
            std::string remote = remote_url(target, m_base_url);
            if (!local.empty()) {
                target = local;
            } else if (!remote.empty()) {
                target = remote;
            }
            m_link_callback(m_opaque, target.c_str());
        }
    }

    bool on_element_click(const litehtml::element::ptr &element) override
    {
        if (!element) {
            return false;
        }
        const char *tag = element->get_tagName();
        if (form_ascii_equal(tag, "label")) {
            const char *for_id = element->get_attr("for", "");
            leonos_form_element *target = nullptr;
            if (for_id && for_id[0]) {
                for (leonos_form_element *item : m_controls) {
                    const char *id = item->get_attr("id", "");
                    if (id && std::strcmp(id, for_id) == 0) {
                        target = item;
                        break;
                    }
                }
            }
            if (!target) {
                target = first_control_descendant(element);
            }
            if (target) {
                activate_control(target);
                return true;
            }
            return false;
        }
        if (!form_ascii_equal(tag, "input") &&
            !form_ascii_equal(tag, "textarea") &&
            !form_ascii_equal(tag, "select") &&
            !form_ascii_equal(tag, "button")) {
            return false;
        }
        leonos_form_element *control =
            static_cast<leonos_form_element *>(element.get());
        if (control->disabled()) {
            return true;
        }
        activate_control(control);
        return true;
    }

    void on_mouse_event(const litehtml::element::ptr &, litehtml::mouse_event) override
    {
    }

    void set_cursor(const char *) override
    {
    }

    void transform_text(std::string &text, litehtml::text_transform transform) override
    {
        if (transform == litehtml::text_transform_capitalize) {
            bool word_start = true;
            for (char &ch : text) {
                if (ch == ' ' || ch == '\t' || ch == '\n') {
                    word_start = true;
                } else if (word_start && ch >= 'a' && ch <= 'z') {
                    ch = (char)(ch - 'a' + 'A');
                    word_start = false;
                } else {
                    word_start = false;
                }
            }
        } else if (transform == litehtml::text_transform_uppercase) {
            for (char &ch : text) {
                if (ch >= 'a' && ch <= 'z') {
                    ch = (char)(ch - 'a' + 'A');
                }
            }
        } else if (transform == litehtml::text_transform_lowercase) {
            for (char &ch : text) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = (char)(ch - 'A' + 'a');
                }
            }
        }
    }

  private:
    static bool is_form_tag(const litehtml::element::ptr &element,
                            const char *name)
    {
        return element && form_ascii_equal(element->get_tagName(), name);
    }

    static litehtml::element::ptr form_owner(const litehtml::element::ptr &element)
    {
        litehtml::element::ptr current = element;
        while (current && !form_ascii_equal(current->get_tagName(), "form")) {
            current = current->parent();
        }
        return current;
    }

    leonos_form_element *first_control_descendant(
        const litehtml::element::ptr &element) const
    {
        if (!element) {
            return nullptr;
        }
        for (leonos_form_element *control : m_controls) {
            if (control == element.get()) {
                return control;
            }
        }
        for (const auto &child : element->children()) {
            leonos_form_element *control = first_control_descendant(child);
            if (control) {
                return control;
            }
        }
        return nullptr;
    }

    void activate_control(leonos_form_element *control)
    {
        if (!control || control->disabled()) {
            return;
        }
        if (control->kind() == form_kind::input_checkbox ||
            control->kind() == form_kind::input_radio) {
            if (control->kind() == form_kind::input_radio) {
                for (leonos_form_element *other : m_controls) {
                    if (other != control && other->kind() == form_kind::input_radio &&
                        other->name() == control->name() &&
                        form_owner(other->shared_from_this()) ==
                            form_owner(control->shared_from_this())) {
                        other->checked(false);
                    }
                }
            }
            if (control->kind() == form_kind::input_checkbox) {
                control->checked(!control->checked());
            } else {
                /* A checked HTML radio remains checked when clicked again. */
                control->checked(true);
            }
            focus(control);
            return;
        }
        if (control->kind() == form_kind::select) {
            focus(control);
            control->choose_option_at(m_pointer_y);
            return;
        }
        if (control->kind() == form_kind::input_submit) {
            focus(control);
            submit(control);
            return;
        }
        if (control->kind() == form_kind::input_button ||
            control->kind() == form_kind::button) {
            focus(control);
            return;
        }
        if (control->kind() == form_kind::input_reset) {
            litehtml::element::ptr owner = form_owner(control->shared_from_this());
            for (leonos_form_element *item : m_controls) {
                if (form_owner(item->shared_from_this()) != owner) {
                    continue;
                }
                if (item->kind() == form_kind::input_text ||
                    item->kind() == form_kind::input_password ||
                    item->kind() == form_kind::textarea ||
                    item->kind() == form_kind::input_checkbox ||
                    item->kind() == form_kind::input_radio) {
                    item->reset_value();
                } else if (item->kind() == form_kind::select) {
                    item->reset_select();
                }
            }
            return;
        }
        focus(control);
        return;
    }

  public:

    void import_css(std::string &text, const std::string &url,
                    std::string &baseurl) override
    {
        std::string path = local_path(url, baseurl);
        if (!path.empty()) {
            if (!read_text_file(path, text)) {
                text.clear();
            } else {
                std::string decoded;
                if (decode_document_text(text.data(), text.size(), decoded)) {
                    text.swap(decoded);
                }
            }
            baseurl = path;
            return;
        }
        std::string resolved = remote_url(url, baseurl);
        if (m_resource_callback && !resolved.empty()) {
            uint8_t *data = nullptr;
            uint32_t size = 0;
            char content_type[64] = {0};
            if (m_resource_callback(m_opaque, resolved.c_str(), &data, &size,
                                    content_type, sizeof(content_type)) == 0 &&
                data && size) {
                if (!decode_document_text((const char *)data,
                                          (std::size_t)size, text)) {
                    text.assign((const char *)data, (std::size_t)size);
                }
                std::free(data);
                baseurl = resolved;
                return;
            }
            std::free(data);
        }
        text.clear();
    }

    void set_clip(const litehtml::position &pos,
                  const litehtml::border_radiuses &) override
    {
        m_clips.push_back(pos);
    }

    void del_clip() override
    {
        if (!m_clips.empty()) {
            m_clips.pop_back();
        }
    }

    void get_viewport(litehtml::position &viewport) const override
    {
        viewport = litehtml::position(litehtml::pixel_t(0),
                                      litehtml::pixel_t(0),
                                      litehtml::pixel_t((int)m_width),
                                      litehtml::pixel_t((int)m_height));
    }

    litehtml::element::ptr create_element(const char *tag,
                                           const litehtml::string_map &attrs,
                                           const std::shared_ptr<litehtml::document> &doc) override
    {
        if (!tag || (!form_ascii_equal(tag, "input") &&
                     !form_ascii_equal(tag, "textarea") &&
                     !form_ascii_equal(tag, "select") &&
                     !form_ascii_equal(tag, "option") &&
                     !form_ascii_equal(tag, "button"))) {
            return {};
        }
        std::shared_ptr<leonos_form_element> control =
            std::make_shared<leonos_form_element>(doc, tag);
        for (const auto &attr : attrs) {
            control->set_attr(attr.first.c_str(), attr.second.c_str());
        }
        if (control->kind() != form_kind::option) {
            m_controls.push_back(control.get());
        }
        return control;
    }

    void clear_form_focus()
    {
        if (m_focus) {
            m_focus->focused(false);
        }
        m_focus = nullptr;
        m_edit.focused = 0;
        m_edit.selecting = 0;
    }

    bool form_input_active() const
    {
        return m_focus != nullptr;
    }

    bool form_input_secure() const
    {
        return m_focus && m_focus->kind() == form_kind::input_password;
    }

    int form_handle_key(const struct leonos_gui_app_event *event)
    {
        if (!event) {
            return 0;
        }
        if (event->pressed && event->keycode == LEONOS_KEY_TAB) {
            focus_next();
            return 1;
        }
        if (!m_focus || m_focus->disabled()) {
            return 0;
        }
        if (event->pressed && event->keycode == LEONOS_KEY_SPACE) {
            if (m_focus->kind() == form_kind::input_checkbox ||
                m_focus->kind() == form_kind::input_radio) {
                if (m_focus->kind() == form_kind::input_radio) {
                    for (leonos_form_element *other : m_controls) {
                        if (other != m_focus &&
                            other->kind() == form_kind::input_radio &&
                            other->name() == m_focus->name() &&
                            form_owner(other->shared_from_this()) ==
                                form_owner(m_focus->shared_from_this())) {
                            other->checked(false);
                        }
                    }
                    m_focus->checked(true);
                } else {
                    m_focus->checked(!m_focus->checked());
                }
                return 1;
            }
            if (m_focus->kind() == form_kind::select) {
                if (m_focus->multiple()) {
                    m_focus->toggle_selected_option();
                } else {
                    m_focus->choose_next_option();
                }
                return 1;
            }
        }
        if (event->pressed && m_focus->kind() == form_kind::select) {
            if (event->keycode == LEONOS_KEY_UP) {
                m_focus->choose_previous_option();
                return 1;
            }
            if (event->keycode == LEONOS_KEY_DOWN) {
                m_focus->choose_next_option();
                return 1;
            }
            if (event->keycode == LEONOS_KEY_ENTER) {
                m_focus->choose_next_option();
                return 1;
            }
        }
        if (event->pressed && event->keycode == LEONOS_KEY_ENTER) {
            if (m_focus->kind() == form_kind::input_submit) {
                submit(m_focus);
                return 1;
            }
            if (m_focus->editable()) {
                submit(m_focus);
                return 1;
            }
        }
        if (!m_focus->editable() || m_focus->readonly()) {
            return 0;
        }
        if (!leonos_ui_edit_state_handle_key(&m_edit, event->keycode,
                                             event->pressed)) {
            return 0;
        }
        if (m_focus->max_length()) {
            const std::size_t limit = std::min<std::size_t>(
                m_focus->max_length(), sizeof(m_edit_buffer) - 1U);
            if (std::strlen(m_edit_buffer) > limit) {
                std::size_t n = limit;
                while (n && ((static_cast<unsigned char>(m_edit_buffer[n]) & 0xc0U) == 0x80U)) {
                    --n;
                }
                m_edit_buffer[n] = 0;
                leonos_ui_edit_state_init(&m_edit, m_edit_buffer,
                                          sizeof(m_edit_buffer));
            }
        }
        m_focus->value(m_edit_buffer);
        return 1;
    }

    uint32_t form_count() const
    {
        std::vector<const litehtml::element *> forms;
        uint32_t count = 0;
        for (leonos_form_element *control : m_controls) {
            litehtml::element::ptr form = form_owner(control->shared_from_this());
            if (form && std::find(forms.begin(), forms.end(), form.get()) == forms.end()) {
                forms.push_back(form.get());
                ++count;
            }
        }
        return count;
    }

    uint32_t form_control_count() const
    {
        return (uint32_t)m_controls.size();
    }

  private:
    void focus(leonos_form_element *control)
    {
        if (!control || !control->focusable()) {
            return;
        }
        if (m_focus && m_focus != control) {
            m_focus->focused(false);
        }
        m_focus = control;
        m_focus->focused(true);
        if (!control->editable()) {
            m_edit.focused = 0;
            m_edit.selecting = 0;
            return;
        }
        control->initialize_textarea_value();
        std::string current = control->value();
        std::size_t n = std::min<std::size_t>(current.size(),
                                              sizeof(m_edit_buffer) - 1U);
        /* Do not split a UTF-8 sequence when copying into the bounded editor
         * buffer; a partial codepoint can make the control appear blank. */
        while (n && ((static_cast<unsigned char>(current[n]) & 0xc0U) == 0x80U)) {
            --n;
        }
        std::memcpy(m_edit_buffer, current.data(), n);
        m_edit_buffer[n] = 0;
        leonos_ui_edit_state_init(&m_edit, m_edit_buffer,
                                  sizeof(m_edit_buffer));
        m_edit.focused = 1;
    }

    void focus_next()
    {
        if (m_controls.empty()) {
            clear_form_focus();
            return;
        }
        std::size_t start = 0;
        if (m_focus) {
            for (std::size_t i = 0; i < m_controls.size(); ++i) {
                if (m_controls[i] == m_focus) {
                    start = i + 1U;
                    break;
                }
            }
        }
        for (std::size_t offset = 0; offset < m_controls.size(); ++offset) {
            std::size_t index = (start + offset) % m_controls.size();
            if (m_controls[index]->focusable()) {
                focus(m_controls[index]);
                return;
            }
        }
        clear_form_focus();
    }

    static void append_encoded(std::string &out, const std::string &text)
    {
        static const char hex[] = "0123456789ABCDEF";
        for (unsigned char ch : text) {
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
                ch == '.' || ch == '~') {
                out.push_back((char)ch);
            } else if (ch == ' ') {
                out.push_back('+');
            } else {
                out.push_back('%');
                out.push_back(hex[ch >> 4]);
                out.push_back(hex[ch & 15U]);
            }
        }
    }

    void submit(leonos_form_element *control)
    {
        if (!m_submit_callback || !control) {
            return;
        }
        litehtml::element::ptr form = form_owner(control->shared_from_this());
        std::string action = form ? form->get_attr("action", m_base_url.c_str())
                                  : m_base_url;
        if (form) {
            std::string resolved_local = local_path(action, m_base_url);
            std::string resolved_remote = remote_url(action, m_base_url);
            if (!resolved_local.empty()) {
                action = resolved_local;
            } else if (!resolved_remote.empty()) {
                action = resolved_remote;
            }
        }
        std::string method = form ? form->get_attr("method", "get") : "get";
        std::string body;
        bool first = true;
        for (leonos_form_element *item : m_controls) {
            if (item->name().empty() || item->disabled() ||
                (item->kind() == form_kind::input_radio && !item->checked()) ||
                (item->kind() == form_kind::input_checkbox && !item->checked())) {
                continue;
            }
            if (item->kind() == form_kind::input_button ||
                item->kind() == form_kind::button ||
                item->kind() == form_kind::input_reset ||
                (item->kind() == form_kind::input_submit && item != control)) {
                continue;
            }
            litehtml::element::ptr owner = form_owner(item->shared_from_this());
            if (owner != form) {
                continue;
            }
            std::vector<std::string> values;
            if (item->kind() == form_kind::select) {
                values = item->selected_values();
            } else {
                values.push_back(item->submit_value());
            }
            for (const std::string &value : values) {
                if (!first) {
                    body.push_back('&');
                }
                first = false;
                append_encoded(body, item->name());
                body.push_back('=');
                append_encoded(body, value);
            }
        }
        m_submit_callback(m_opaque, action.c_str(), method.c_str(), body.c_str());
    }

  public:
    void get_media_features(litehtml::media_features &media) const override
    {
        media.width = litehtml::pixel_t((int)m_width);
        media.height = litehtml::pixel_t((int)m_height);
        media.device_width = media.width;
        media.device_height = media.height;
        media.color = 8;
        media.monochrome = 0;
        media.resolution = 96;
        media.type = litehtml::media_type_screen;
    }

    void get_language(std::string &language, std::string &culture) const override
    {
        language = "zh";
        culture = "zh-CN";
    }

    void set_size(uint32_t width, uint32_t height)
    {
        m_width = width;
        m_height = height;
    }

    void set_pointer_y(int32_t y)
    {
        m_pointer_y = y;
    }

    uint32_t height() const
    {
        return m_height;
    }

    void begin_draw(const litehtml::position &clip)
    {
        m_draw_clip = clip;
        m_has_draw_clip = true;
        m_clips.clear();
    }

    void end_draw()
    {
        m_clips.clear();
        m_has_draw_clip = false;
    }

  private:
    static litehtml::web_color gradient_sample(
        const std::vector<litehtml::background_layer::color_point> &points,
        float factor)
    {
        if (points.empty()) {
            return litehtml::web_color(255, 255, 255);
        }
        if (factor <= points.front().offset) {
            return points.front().color;
        }
        for (std::size_t i = 1; i < points.size(); ++i) {
            if (factor <= points[i].offset) {
                const auto &a = points[i - 1U];
                const auto &b = points[i];
                float span = b.offset - a.offset;
                float local = span > 0.0001f ? (factor - a.offset) / span : 0.0f;
                local = std::max(0.0f, std::min(1.0f, local));
                return litehtml::web_color(
                    (litehtml::byte)(a.color.red +
                                     (b.color.red - a.color.red) * local + 0.5f),
                    (litehtml::byte)(a.color.green +
                                     (b.color.green - a.color.green) * local + 0.5f),
                    (litehtml::byte)(a.color.blue +
                                     (b.color.blue - a.color.blue) * local + 0.5f),
                    (litehtml::byte)(a.color.alpha +
                                     (b.color.alpha - a.color.alpha) * local + 0.5f));
            }
        }
        return points.back().color;
    }

    template <typename Factor>
    void draw_gradient(
        struct leonos_ui_surface *surface,
        const litehtml::background_layer &layer,
        const std::vector<litehtml::background_layer::color_point> &points,
        Factor factor) const
    {
        int32_t left = std::max<int32_t>(signed_px(layer.clip_box.x), 0);
        int32_t top = std::max<int32_t>(signed_px(layer.clip_box.y), 0);
        int32_t right = std::min<int32_t>(signed_px(layer.clip_box.right()),
                                          (int32_t)surface->width);
        int32_t bottom = std::min<int32_t>(signed_px(layer.clip_box.bottom()),
                                           (int32_t)surface->height);
        for (int32_t y = top; y < bottom; ++y) {
            for (int32_t x = left; x < right; ++x) {
                if (!point_visible(x, y)) {
                    continue;
                }
                litehtml::web_color color =
                    gradient_sample(points, factor((float)x + 0.5f,
                                                   (float)y + 0.5f));
                uint32_t *pixel = surface->pixels +
                                  (uint64_t)(uint32_t)y * surface->stride +
                                  (uint32_t)x;
                *pixel = color.alpha == 255U
                             ? color_value(color)
                             : blend_color(*pixel, color_value(color), color.alpha);
            }
        }
    }

    bool point_visible(int32_t x, int32_t y) const
    {
        if (m_has_draw_clip &&
            (x < signed_px(m_draw_clip.x) ||
             y < signed_px(m_draw_clip.y) ||
             x >= signed_px(m_draw_clip.right()) ||
             y >= signed_px(m_draw_clip.bottom()))) {
            return false;
        }
        for (const litehtml::position &clip : m_clips) {
            if (x < signed_px(clip.x) || y < signed_px(clip.y) ||
                x >= signed_px(clip.right()) || y >= signed_px(clip.bottom())) {
                return false;
            }
        }
        return true;
    }

    bool box_intersects_clips(float x, float y, float width, float height) const
    {
        int32_t left = (int32_t)(x >= 0.0f ? x : x - 1.0f);
        int32_t top = (int32_t)(y >= 0.0f ? y : y - 1.0f);
        int32_t right = (int32_t)(x + width + 0.5f);
        int32_t bottom = (int32_t)(y + height + 0.5f);
        if (right <= left || bottom <= top) {
            return false;
        }
        if (m_has_draw_clip &&
            (right <= signed_px(m_draw_clip.x) ||
             bottom <= signed_px(m_draw_clip.y) ||
             left >= signed_px(m_draw_clip.right()) ||
             top >= signed_px(m_draw_clip.bottom()))) {
            return false;
        }
        for (const litehtml::position &clip : m_clips) {
            if (right <= signed_px(clip.x) || bottom <= signed_px(clip.y) ||
                left >= signed_px(clip.right()) ||
                top >= signed_px(clip.bottom())) {
                return false;
            }
        }
        return true;
    }

    void fill_clipped(struct leonos_ui_surface *surface, int32_t x, int32_t y,
                      uint32_t width, uint32_t height, uint32_t color,
                      uint8_t alpha = 255U) const
    {
        if (!surface || !surface->pixels || !width || !height) {
            return;
        }
        int32_t left = std::max<int32_t>(x, 0);
        int32_t top = std::max<int32_t>(y, 0);
        int32_t right = std::min<int32_t>(x + (int32_t)width,
                                          (int32_t)surface->width);
        int32_t bottom = std::min<int32_t>(y + (int32_t)height,
                                           (int32_t)surface->height);
        auto intersect = [&left, &top, &right, &bottom](const litehtml::position &clip) {
            left = std::max(left, signed_px(clip.x));
            top = std::max(top, signed_px(clip.y));
            right = std::min(right, signed_px(clip.right()));
            bottom = std::min(bottom, signed_px(clip.bottom()));
        };
        if (m_has_draw_clip) {
            intersect(m_draw_clip);
        }
        for (const litehtml::position &clip : m_clips) {
            intersect(clip);
        }
        if (right <= left || bottom <= top) {
            return;
        }
        for (int32_t row = top; row < bottom; ++row) {
            uint32_t *dst = surface->pixels +
                            (uint64_t)(uint32_t)row * surface->stride +
                            (uint32_t)left;
            for (int32_t col = left; col < right; ++col) {
                *dst = alpha == 255U ? color : blend_color(*dst, color, alpha);
                ++dst;
            }
        }
    }

    struct image_entry {
        std::string path;
        uint32_t *pixels = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    image_entry *image_for(const std::string &path)
    {
        for (image_entry &entry : m_images) {
            if (entry.path == path) {
                return entry.pixels ? &entry : nullptr;
            }
        }
        image_entry entry;
        entry.path = path;
        if (leonos_png_decode_file(path.c_str(), &entry.pixels,
                                   &entry.width, &entry.height) != 0) {
            m_images.push_back(entry);
            return nullptr;
        }
        m_images.push_back(entry);
        return &m_images.back();
    }

    image_entry *image_for_url(const std::string &url,
                               const std::string &base_url)
    {
        const std::string &effective_base = base_url.empty() ? m_base_url : base_url;
        std::string path = local_path(url, effective_base);
        if (!path.empty()) {
            return image_for(path);
        }
        if (!m_resource_callback) {
            return nullptr;
        }
        std::string key = remote_url(url, effective_base);
        if (key.empty()) {
            return nullptr;
        }
        for (image_entry &entry : m_images) {
            if (entry.path == key) {
                return entry.pixels ? &entry : nullptr;
            }
        }
        uint8_t *data = nullptr;
        uint32_t size = 0;
        char content_type[64] = {0};
        image_entry entry;
        entry.path = key;
        if (m_resource_callback(m_opaque, key.c_str(), &data, &size,
                                content_type, sizeof(content_type)) != 0 ||
            !data || !size ||
            leonos_png_decode_memory(data, size, &entry.pixels,
                                     &entry.width, &entry.height) != 0) {
            std::free(data);
            m_images.push_back(entry);
            return nullptr;
        }
        std::free(data);
        m_images.push_back(entry);
        return &m_images.back();
    }

    void release_images()
    {
        for (image_entry &entry : m_images) {
            if (entry.pixels) {
                leonos_png_free(entry.pixels);
            }
        }
        m_images.clear();
    }

    browser_litehtml_link_callback m_link_callback;
    browser_litehtml_title_callback m_title_callback;
    browser_litehtml_submit_callback m_submit_callback;
    browser_litehtml_resource_callback m_resource_callback;
    void *m_opaque;
    std::string m_base_url;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    int32_t m_pointer_y = 0;
    std::vector<litehtml::position> m_clips;
    litehtml::position m_draw_clip;
    bool m_has_draw_clip = false;
    std::vector<image_entry> m_images;
    std::vector<leonos_form_element *> m_controls;
    leonos_form_element *m_focus = nullptr;
    struct leonos_ui_edit_state m_edit = {};
    char m_edit_buffer[256] = {};
};

} // namespace

struct browser_litehtml_document {
    leonos_container container;
    litehtml::document::ptr document;

    browser_litehtml_document(browser_litehtml_link_callback link_callback,
                              browser_litehtml_title_callback title_callback,
                              browser_litehtml_submit_callback submit_callback,
                              browser_litehtml_resource_callback resource_callback,
                              void *opaque) :
        container(link_callback, title_callback, submit_callback,
                  resource_callback, opaque)
    {
    }
};

namespace {

static litehtml::position clip_position(const browser_litehtml_document *document,
                                        uint32_t x, uint32_t y,
                                        uint32_t width, uint32_t height)
{
    (void)document;
    return litehtml::position(litehtml::pixel_t((int)x),
                              litehtml::pixel_t((int)y),
                              litehtml::pixel_t((int)width),
                              litehtml::pixel_t((int)height));
}

} // namespace

extern "C" struct browser_litehtml_document *browser_litehtml_create(
    const char *source, const char *base_url,
    browser_litehtml_link_callback link_callback,
    browser_litehtml_title_callback title_callback,
    browser_litehtml_submit_callback submit_callback,
    browser_litehtml_resource_callback resource_callback,
    void *opaque, uint32_t width, uint32_t height)
{
    if (!source) {
        source = "";
    }
    browser_litehtml_document *wrapper = new (std::nothrow)
        browser_litehtml_document(link_callback, title_callback,
                                   submit_callback,
                                   resource_callback, opaque);
    if (!wrapper) {
        return nullptr;
    }
    wrapper->container.set_size(width ? width : 1U, height ? height : 1U);
    wrapper->container.set_base_url(base_url ? base_url : "");
    wrapper->document = litehtml::document::createFromString(
        litehtml::estring(source), &wrapper->container);
    if (!wrapper->document) {
        delete wrapper;
        return nullptr;
    }
    wrapper->document->render(litehtml::pixel_t((int)(width ? width : 1U)));
    return wrapper;
}

extern "C" void browser_litehtml_destroy(
    struct browser_litehtml_document *document)
{
    delete document;
}

extern "C" void browser_litehtml_form_clear_focus(
    struct browser_litehtml_document *document)
{
    if (document) {
        document->container.clear_form_focus();
    }
}

extern "C" int browser_litehtml_form_input_active(
    const struct browser_litehtml_document *document)
{
    return document && document->container.form_input_active();
}

extern "C" int browser_litehtml_form_input_secure(
    const struct browser_litehtml_document *document)
{
    return document && document->container.form_input_secure();
}

extern "C" int browser_litehtml_form_handle_key(
    struct browser_litehtml_document *document,
    const struct leonos_gui_app_event *event)
{
    return document ? document->container.form_handle_key(event) : 0;
}

extern "C" uint32_t browser_litehtml_form_count(
    const struct browser_litehtml_document *document)
{
    return document ? document->container.form_count() : 0U;
}

extern "C" uint32_t browser_litehtml_form_control_count(
    const struct browser_litehtml_document *document)
{
    return document ? document->container.form_control_count() : 0U;
}

extern "C" int browser_litehtml_resize(
    struct browser_litehtml_document *document, uint32_t width,
    uint32_t height)
{
    if (!document || !document->document || !width || !height) {
        return -1;
    }
    document->container.set_size(width, height);
    document->document->render(litehtml::pixel_t((int)width));
    return 0;
}

extern "C" void browser_litehtml_draw(
    struct browser_litehtml_document *document,
    struct leonos_ui_surface *surface,
    int32_t origin_x, int32_t origin_y,
    int32_t scroll_x, int32_t scroll_y,
    uint32_t clip_x, uint32_t clip_y,
    uint32_t clip_w, uint32_t clip_h)
{
    if (!document || !document->document || !surface) {
        return;
    }
    litehtml::position clip = clip_position(document, clip_x, clip_y,
                                            clip_w, clip_h);
    document->container.begin_draw(clip);
    document->document->draw((litehtml::uint_ptr)(uintptr_t)surface,
                             (litehtml::pixel_t)(origin_x - scroll_x),
                             (litehtml::pixel_t)(origin_y - scroll_y), &clip);
    document->container.end_draw();
}

extern "C" uint32_t browser_litehtml_content_width(
    const struct browser_litehtml_document *document)
{
    return document && document->document
               ? px_value(document->document->width())
               : 0U;
}

extern "C" uint32_t browser_litehtml_content_height(
    const struct browser_litehtml_document *document)
{
    return document && document->document
               ? px_value(document->document->height())
               : 0U;
}

extern "C" int browser_litehtml_mouse_move(
    struct browser_litehtml_document *document, int32_t x, int32_t y)
{
    if (!document || !document->document) {
        return 0;
    }
    return document->document->on_mouse_over((litehtml::pixel_t)x,
                                             (litehtml::pixel_t)y,
                                             (litehtml::pixel_t)x,
                                             (litehtml::pixel_t)y,
                                             [](const litehtml::position &) {});
}

extern "C" int browser_litehtml_lbutton_down(
    struct browser_litehtml_document *document, int32_t x, int32_t y)
{
    if (!document || !document->document) {
        return 0;
    }
    document->container.set_pointer_y(y);
    return document->document->on_lbutton_down((litehtml::pixel_t)x,
                                               (litehtml::pixel_t)y,
                                               (litehtml::pixel_t)x,
                                               (litehtml::pixel_t)y,
                                               [](const litehtml::position &) {});
}

extern "C" int browser_litehtml_lbutton_up(
    struct browser_litehtml_document *document, int32_t x, int32_t y)
{
    if (!document || !document->document) {
        return 0;
    }
    document->container.set_pointer_y(y);
    return document->document->on_lbutton_up((litehtml::pixel_t)x,
                                             (litehtml::pixel_t)y,
                                             (litehtml::pixel_t)x,
                                             (litehtml::pixel_t)y,
                                             [](const litehtml::position &) {});
}

extern "C" int browser_litehtml_mouse_leave(
    struct browser_litehtml_document *document)
{
    if (!document || !document->document) {
        return 0;
    }
    return document->document->on_mouse_leave(
        [](const litehtml::position &) {});
}
