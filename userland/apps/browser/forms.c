#include "browser.h"

#define FORM_BODY_CAP 768U

struct form_old_value {
    uint32_t ordinal;
    uint32_t flags;
    char name[BROWSER_FORM_NAME_CAP];
    char value[BROWSER_FORM_VALUE_CAP];
    uint8_t kind;
};

static uint8_t form_skip_restore_once;

static char form_tolower(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? (char)(ch - 'A' + 'a') : ch;
}

static int form_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int form_text_eq(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && a[i] == b[i]) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static int form_text_eq_ignore_case(const char *a, const char *b)
{
    uint32_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && form_tolower(a[i]) == form_tolower(b[i])) {
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static int form_starts_with(const char *text, const char *prefix)
{
    uint32_t i = 0;
    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static uint32_t form_restore_flags(uint32_t current_flags, uint32_t old_flags)
{
    return (current_flags & (BROWSER_FORM_CONTROL_DISABLED |
                             BROWSER_FORM_CONTROL_READONLY |
                             BROWSER_FORM_CONTROL_PLACEHOLDER)) |
           (old_flags & BROWSER_FORM_CONTROL_CHECKED);
}

static void form_trim_copy(char *dst, uint32_t cap, const char *src, uint32_t len)
{
    uint32_t start = 0;
    uint32_t end = len;
    uint32_t pos = 0;
    if (!dst || cap == 0) {
        return;
    }
    while (start < len && form_is_space(src[start])) {
        ++start;
    }
    while (end > start && form_is_space(src[end - 1U])) {
        --end;
    }
    while (start < end && pos + 1U < cap) {
        dst[pos++] = src[start++];
    }
    dst[pos] = 0;
}

static int form_attr_name_eq(const char *text, uint32_t len, const char *name)
{
    uint32_t name_len = (uint32_t)strlen(name);
    if (len != name_len) {
        return 0;
    }
    for (uint32_t i = 0; i < len; ++i) {
        if (form_tolower(text[i]) != form_tolower(name[i])) {
            return 0;
        }
    }
    return 1;
}

static int form_extract_attr(const char *tag, const char *name, char *out, uint32_t cap)
{
    uint32_t i = 0;
    if (out && cap) {
        out[0] = 0;
    }
    if (!tag || !name || !out || cap == 0) {
        return 0;
    }
    while (tag[i]) {
        uint32_t name_start;
        uint32_t name_end;
        uint32_t value_start;
        uint32_t value_end;
        char quote = 0;
        while (tag[i] && (form_is_space(tag[i]) || tag[i] == '<' ||
                          tag[i] == '/' || tag[i] == '>')) {
            ++i;
        }
        name_start = i;
        while (tag[i] && !form_is_space(tag[i]) && tag[i] != '=' &&
               tag[i] != '/' && tag[i] != '>') {
            ++i;
        }
        name_end = i;
        while (tag[i] && form_is_space(tag[i])) {
            ++i;
        }
        if (tag[i] != '=') {
            continue;
        }
        ++i;
        while (tag[i] && form_is_space(tag[i])) {
            ++i;
        }
        if (tag[i] == '"' || tag[i] == '\'') {
            quote = tag[i++];
        }
        value_start = i;
        if (quote) {
            while (tag[i] && tag[i] != quote) {
                ++i;
            }
        } else {
            while (tag[i] && !form_is_space(tag[i]) && tag[i] != '>') {
                ++i;
            }
        }
        value_end = i;
        if (quote && tag[i] == quote) {
            ++i;
        }
        if (form_attr_name_eq(tag + name_start, name_end - name_start, name)) {
            form_trim_copy(out, cap, tag + value_start, value_end - value_start);
            return 1;
        }
    }
    return 0;
}

static int form_has_attr(const char *tag, const char *name)
{
    uint32_t i = 0;
    if (!tag || !name) {
        return 0;
    }
    while (tag[i]) {
        uint32_t name_start;
        uint32_t name_end;
        while (tag[i] && (form_is_space(tag[i]) || tag[i] == '<' ||
                          tag[i] == '/' || tag[i] == '>')) {
            ++i;
        }
        name_start = i;
        while (tag[i] && !form_is_space(tag[i]) && tag[i] != '=' &&
               tag[i] != '/' && tag[i] != '>') {
            ++i;
        }
        name_end = i;
        if (name_end > name_start &&
            form_attr_name_eq(tag + name_start, name_end - name_start, name)) {
            return 1;
        }
        while (tag[i] && form_is_space(tag[i])) {
            ++i;
        }
        if (tag[i] == '=') {
            char quote = 0;
            ++i;
            while (tag[i] && form_is_space(tag[i])) {
                ++i;
            }
            if (tag[i] == '"' || tag[i] == '\'') {
                quote = tag[i++];
            }
            if (quote) {
                while (tag[i] && tag[i] != quote) {
                    ++i;
                }
                if (tag[i] == quote) {
                    ++i;
                }
            } else {
                while (tag[i] && !form_is_space(tag[i]) && tag[i] != '>') {
                    ++i;
                }
            }
        }
    }
    return 0;
}

static int form_tag_name(const char *tag, char *out, uint32_t cap, uint8_t *closing)
{
    uint32_t i = 0;
    uint32_t pos = 0;
    if (!tag || !out || cap == 0) {
        return 0;
    }
    while (form_is_space(tag[i])) {
        ++i;
    }
    *closing = 0;
    if (tag[i] == '/') {
        *closing = 1;
        ++i;
        while (form_is_space(tag[i])) {
            ++i;
        }
    }
    while (tag[i] && !form_is_space(tag[i]) && tag[i] != '/' &&
           tag[i] != '>' && pos + 1U < cap) {
        out[pos++] = form_tolower(tag[i++]);
    }
    out[pos] = 0;
    return out[0] != 0;
}

static uint32_t form_copy_tag_at(const char *source, uint32_t pos,
                                 char *tag, uint32_t tag_cap)
{
    uint32_t out = 0;
    if (!source || source[pos] != '<' || !tag || tag_cap == 0) {
        return 0;
    }
    ++pos;
    while (source[pos] && source[pos] != '>' && out + 1U < tag_cap) {
        tag[out++] = source[pos++];
    }
    tag[out] = 0;
    if (source[pos] == '>') {
        ++pos;
    }
    return pos;
}

static void form_append_decoded_entity(char *dst, uint32_t *pos,
                                       uint32_t cap, const char *entity,
                                       uint32_t len)
{
    if (len == 3U && form_attr_name_eq(entity, len, "amp")) {
        append_char(dst, pos, cap, '&');
    } else if (len == 2U && form_attr_name_eq(entity, len, "lt")) {
        append_char(dst, pos, cap, '<');
    } else if (len == 2U && form_attr_name_eq(entity, len, "gt")) {
        append_char(dst, pos, cap, '>');
    } else if (len == 4U && form_attr_name_eq(entity, len, "quot")) {
        append_char(dst, pos, cap, '"');
    } else if (len == 4U && form_attr_name_eq(entity, len, "apos")) {
        append_char(dst, pos, cap, '\'');
    } else if (len == 4U && form_attr_name_eq(entity, len, "nbsp")) {
        append_char(dst, pos, cap, ' ');
    } else {
        append_char(dst, pos, cap, '&');
        for (uint32_t i = 0; i < len; ++i) {
            append_char(dst, pos, cap, entity[i]);
        }
        append_char(dst, pos, cap, ';');
    }
}

static void form_capture_html_text(const char *source, uint32_t start,
                                   uint32_t end, char *out, uint32_t cap)
{
    uint32_t pos = start;
    char tmp[BROWSER_FORM_VALUE_CAP];
    uint32_t tmp_pos = 0;
    if (out && cap) {
        out[0] = 0;
    }
    tmp[0] = 0;
    while (source && source[pos] && pos < end && tmp_pos + 1U < sizeof(tmp)) {
        if (source[pos] == '<') {
            while (pos < end && source[pos] && source[pos] != '>') {
                ++pos;
            }
            if (pos < end && source[pos] == '>') {
                ++pos;
            }
            continue;
        }
        if (source[pos] == '&') {
            char entity[16];
            uint32_t entity_pos = 0;
            uint32_t scan = pos + 1U;
            while (scan < end && source[scan] && source[scan] != ';' &&
                   entity_pos + 1U < sizeof(entity)) {
                entity[entity_pos++] = source[scan++];
            }
            entity[entity_pos] = 0;
            if (scan < end && source[scan] == ';') {
                form_append_decoded_entity(tmp, &tmp_pos, sizeof(tmp),
                                           entity, entity_pos);
                pos = scan + 1U;
                continue;
            }
        }
        append_char(tmp, &tmp_pos, sizeof(tmp), source[pos++]);
    }
    form_trim_copy(out, cap, tmp, (uint32_t)strlen(tmp));
}

static void form_capture_button_label(const char *source, uint32_t start,
                                      char *out, uint32_t cap)
{
    uint32_t end = start;
    while (source && source[end] && source[end] != '<') {
        ++end;
    }
    form_capture_html_text(source, start, end, out, cap);
}

static void form_collect_old_values(struct form_old_value *old_values,
                                    uint32_t *old_count)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < browser_form_control_count &&
                         count < BROWSER_MAX_FORM_CONTROLS; ++i) {
        const struct browser_form_control *control = &browser_form_controls[i];
        if (control->kind == BROWSER_FORM_CONTROL_TEXT ||
            control->kind == BROWSER_FORM_CONTROL_PASSWORD ||
            control->kind == BROWSER_FORM_CONTROL_CHECKBOX ||
            control->kind == BROWSER_FORM_CONTROL_RADIO ||
            control->kind == BROWSER_FORM_CONTROL_SELECT ||
            control->kind == BROWSER_FORM_CONTROL_TEXTAREA) {
            old_values[count].ordinal = i;
            old_values[count].flags = control->flags;
            old_values[count].kind = control->kind;
            copy_text(old_values[count].name, sizeof(old_values[count].name), control->name);
            copy_text(old_values[count].value, sizeof(old_values[count].value), control->value);
            ++count;
        }
    }
    *old_count = count;
}

static void form_restore_old_value(struct browser_form_control *control,
                                   uint32_t control_index,
                                   const struct form_old_value *old_values,
                                   uint32_t old_count)
{
    if (!control ||
        !(control->kind == BROWSER_FORM_CONTROL_TEXT ||
          control->kind == BROWSER_FORM_CONTROL_PASSWORD ||
          control->kind == BROWSER_FORM_CONTROL_CHECKBOX ||
          control->kind == BROWSER_FORM_CONTROL_RADIO ||
          control->kind == BROWSER_FORM_CONTROL_SELECT ||
          control->kind == BROWSER_FORM_CONTROL_TEXTAREA)) {
        return;
    }
    for (uint32_t i = 0; i < old_count; ++i) {
        if (control->name[0] && old_values[i].kind == control->kind &&
            form_text_eq(old_values[i].name, control->name) &&
            (control->kind == BROWSER_FORM_CONTROL_TEXT ||
             control->kind == BROWSER_FORM_CONTROL_PASSWORD ||
             control->kind == BROWSER_FORM_CONTROL_SELECT ||
             control->kind == BROWSER_FORM_CONTROL_TEXTAREA ||
             form_text_eq(old_values[i].value, control->value))) {
            copy_text(control->value, sizeof(control->value), old_values[i].value);
            control->flags = form_restore_flags(control->flags,
                                                old_values[i].flags);
            return;
        }
    }
    for (uint32_t i = 0; i < old_count; ++i) {
        if (old_values[i].kind == control->kind &&
            old_values[i].ordinal == control_index) {
            copy_text(control->value, sizeof(control->value), old_values[i].value);
            control->flags = form_restore_flags(control->flags,
                                                old_values[i].flags);
            return;
        }
    }
}

static int form_add_form(const char *tag, const char *base_url,
                         int *current_form)
{
    char method[BROWSER_FORM_METHOD_CAP];
    char action[BROWSER_URL_CAP];
    uint32_t index;
    if (browser_form_count >= BROWSER_MAX_FORMS) {
        *current_form = -1;
        return -1;
    }
    index = browser_form_count++;
    browser_forms[index] = (struct browser_form){0};
    browser_forms[index].first_control = browser_form_control_count;
    form_extract_attr(tag, "method", method, sizeof(method));
    form_extract_attr(tag, "action", action, sizeof(action));
    if (!method[0]) {
        copy_text(method, sizeof(method), "get");
    }
    for (uint32_t i = 0; method[i]; ++i) {
        method[i] = form_tolower(method[i]);
    }
    copy_text(browser_forms[index].method, sizeof(browser_forms[index].method),
              form_text_eq_ignore_case(method, "post") ? "post" : "get");
    if (action[0]) {
        leonos_http_resolve_url(base_url && base_url[0] ? base_url : current_location,
                                action,
                                browser_forms[index].action,
                                sizeof(browser_forms[index].action));
    } else {
        copy_text(browser_forms[index].action, sizeof(browser_forms[index].action),
                  current_location);
    }
    *current_form = (int)index;
    return 0;
}

static uint32_t form_add_control(uint8_t kind, int form_index,
                                 const char *name, const char *value,
                                 const char *label, uint32_t flags)
{
    uint32_t index;
    if (form_index < 0 || browser_form_control_count >= BROWSER_MAX_FORM_CONTROLS) {
        return BROWSER_MAX_FORM_CONTROLS;
    }
    index = browser_form_control_count++;
    browser_form_controls[index] = (struct browser_form_control){0};
    browser_form_controls[index].kind = kind;
    browser_form_controls[index].form_index = (uint8_t)form_index;
    browser_form_controls[index].flags = flags;
    browser_form_controls[index].first_option = BROWSER_MAX_FORM_OPTIONS;
    copy_text(browser_form_controls[index].name,
              sizeof(browser_form_controls[index].name), name);
    copy_text(browser_form_controls[index].value,
              sizeof(browser_form_controls[index].value), value);
    copy_text(browser_form_controls[index].label,
              sizeof(browser_form_controls[index].label),
              label && label[0] ? label : name && name[0] ? name : "Submit");
    browser_forms[(uint32_t)form_index].control_count =
        browser_form_control_count - browser_forms[(uint32_t)form_index].first_control;
    return index;
}

static uint32_t form_add_option(uint32_t control_index, const char *value,
                                const char *label, uint8_t selected)
{
    struct browser_form_control *control;
    uint32_t index;
    uint32_t first;
    if (control_index >= browser_form_control_count ||
        browser_form_option_count >= BROWSER_MAX_FORM_OPTIONS) {
        return BROWSER_MAX_FORM_OPTIONS;
    }
    control = &browser_form_controls[control_index];
    first = control->option_count == 0;
    index = browser_form_option_count++;
    browser_form_options[index] = (struct browser_form_option){0};
    browser_form_options[index].control_index = control_index;
    copy_text(browser_form_options[index].value,
              sizeof(browser_form_options[index].value),
              value && value[0] ? value : label);
    copy_text(browser_form_options[index].label,
              sizeof(browser_form_options[index].label),
              label && label[0] ? label : browser_form_options[index].value);
    if (first) {
        control->first_option = index;
    }
    ++control->option_count;
    if (selected || first) {
        copy_text(control->value, sizeof(control->value),
                  browser_form_options[index].value);
        copy_text(control->label, sizeof(control->label),
                  browser_form_options[index].label);
    }
    return index;
}

static void form_select_sync_label(uint32_t control_index)
{
    struct browser_form_control *control;
    if (control_index >= browser_form_control_count) {
        return;
    }
    control = &browser_form_controls[control_index];
    if (control->kind != BROWSER_FORM_CONTROL_SELECT ||
        control->first_option >= BROWSER_MAX_FORM_OPTIONS) {
        return;
    }
    for (uint32_t i = 0; i < control->option_count; ++i) {
        uint32_t option_index = control->first_option + i;
        if (option_index >= browser_form_option_count) {
            break;
        }
        if (form_text_eq(browser_form_options[option_index].value,
                         control->value)) {
            copy_text(control->label, sizeof(control->label),
                      browser_form_options[option_index].label);
            return;
        }
    }
}

static uint32_t form_parse_input(const char *tag, int current_form,
                                 const struct form_old_value *old_values,
                                 uint32_t old_count)
{
    char type[24];
    char name[BROWSER_FORM_NAME_CAP];
    char value[BROWSER_FORM_VALUE_CAP];
    char label[BROWSER_FORM_LABEL_CAP];
    uint8_t kind = BROWSER_FORM_CONTROL_TEXT;
    uint32_t flags = 0;
    uint32_t index;
    if (current_form < 0) {
        return BROWSER_MAX_FORM_CONTROLS;
    }
    type[0] = 0;
    name[0] = 0;
    value[0] = 0;
    label[0] = 0;
    form_extract_attr(tag, "type", type, sizeof(type));
    form_extract_attr(tag, "name", name, sizeof(name));
    form_extract_attr(tag, "value", value, sizeof(value));
    form_extract_attr(tag, "placeholder", label, sizeof(label));
    if (label[0]) {
        flags |= BROWSER_FORM_CONTROL_PLACEHOLDER;
    }
    if (form_has_attr(tag, "disabled")) {
        flags |= BROWSER_FORM_CONTROL_DISABLED;
    }
    if (!type[0] || form_text_eq_ignore_case(type, "text") ||
        form_text_eq_ignore_case(type, "email") ||
        form_text_eq_ignore_case(type, "search") ||
        form_text_eq_ignore_case(type, "url") ||
        form_text_eq_ignore_case(type, "tel") ||
        form_text_eq_ignore_case(type, "number")) {
        kind = BROWSER_FORM_CONTROL_TEXT;
    } else if (form_text_eq_ignore_case(type, "password")) {
        kind = BROWSER_FORM_CONTROL_PASSWORD;
    } else if (form_text_eq_ignore_case(type, "hidden")) {
        kind = BROWSER_FORM_CONTROL_HIDDEN;
    } else if (form_text_eq_ignore_case(type, "checkbox")) {
        kind = BROWSER_FORM_CONTROL_CHECKBOX;
        if (!value[0]) {
            copy_text(value, sizeof(value), "on");
        }
        if (form_has_attr(tag, "checked")) {
            flags |= BROWSER_FORM_CONTROL_CHECKED;
        }
        if (!label[0]) {
            copy_text(label, sizeof(label), name[0] ? name : value);
        }
    } else if (form_text_eq_ignore_case(type, "radio")) {
        kind = BROWSER_FORM_CONTROL_RADIO;
        if (!value[0]) {
            copy_text(value, sizeof(value), "on");
        }
        if (form_has_attr(tag, "checked")) {
            flags |= BROWSER_FORM_CONTROL_CHECKED;
        }
        if (!label[0]) {
            copy_text(label, sizeof(label), value[0] ? value : name);
        }
    } else if (form_text_eq_ignore_case(type, "submit")) {
        kind = BROWSER_FORM_CONTROL_SUBMIT;
        if (!label[0]) {
            copy_text(label, sizeof(label), value[0] ? value : "Submit");
        }
    } else if (form_text_eq_ignore_case(type, "reset")) {
        kind = BROWSER_FORM_CONTROL_RESET;
        if (!label[0]) {
            copy_text(label, sizeof(label), value[0] ? value : "Reset");
        }
    } else {
        return BROWSER_MAX_FORM_CONTROLS;
    }
    if ((kind == BROWSER_FORM_CONTROL_TEXT ||
         kind == BROWSER_FORM_CONTROL_PASSWORD) &&
        form_has_attr(tag, "readonly")) {
        flags |= BROWSER_FORM_CONTROL_READONLY;
    }
    index = form_add_control(kind, current_form, name, value,
                             label[0] ? label : name, flags);
    if (index < BROWSER_MAX_FORM_CONTROLS) {
        form_restore_old_value(&browser_form_controls[index], index,
                               old_values, old_count);
    }
    return index;
}

static char form_inline_source[BROWSER_SOURCE_CAP];

static void form_inline_append_char(char *dst, uint32_t *pos,
                                    uint32_t cap, char ch)
{
    if (dst && pos && *pos + 1U < cap) {
        dst[*pos] = ch;
        ++(*pos);
        dst[*pos] = 0;
    }
}

static void form_inline_append_text(char *dst, uint32_t *pos,
                                    uint32_t cap, const char *src)
{
    while (src && *src) {
        form_inline_append_char(dst, pos, cap, *src++);
    }
}

static void form_inline_append_range(char *dst, uint32_t *pos,
                                     uint32_t cap, const char *src,
                                     uint32_t len)
{
    for (uint32_t i = 0; src && i < len; ++i) {
        form_inline_append_char(dst, pos, cap, src[i]);
    }
}

static void form_inline_append_u32(char *dst, uint32_t *pos,
                                   uint32_t cap, uint32_t value)
{
    char tmp[12];
    uint32_t n = 0;
    if (value == 0) {
        form_inline_append_char(dst, pos, cap, '0');
        return;
    }
    while (value && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n) {
        form_inline_append_char(dst, pos, cap, tmp[--n]);
    }
}

static void form_inline_append_html_text(char *dst, uint32_t *pos,
                                         uint32_t cap, const char *src)
{
    while (src && *src) {
        switch (*src) {
        case '&':
            form_inline_append_text(dst, pos, cap, "&amp;");
            break;
        case '<':
            form_inline_append_text(dst, pos, cap, "&lt;");
            break;
        case '>':
            form_inline_append_text(dst, pos, cap, "&gt;");
            break;
        default:
            form_inline_append_char(dst, pos, cap, *src);
            break;
        }
        ++src;
    }
}

static void form_inline_append_control(char *dst, uint32_t *pos,
                                       uint32_t cap, uint32_t control_index)
{
    const struct browser_form_control *control;
    if (control_index >= browser_form_control_count) {
        return;
    }
    control = &browser_form_controls[control_index];
    if (control->kind == BROWSER_FORM_CONTROL_HIDDEN) {
        return;
    }
    form_inline_append_text(dst, pos, cap, "<a href=\"");
    if (control->kind == BROWSER_FORM_CONTROL_SUBMIT) {
        form_inline_append_text(dst, pos, cap, "form:submit:");
    } else if (control->kind == BROWSER_FORM_CONTROL_RESET) {
        form_inline_append_text(dst, pos, cap, "form:reset:");
    } else if (control->kind == BROWSER_FORM_CONTROL_CHECKBOX ||
               control->kind == BROWSER_FORM_CONTROL_RADIO ||
               control->kind == BROWSER_FORM_CONTROL_SELECT) {
        form_inline_append_text(dst, pos, cap, "form:toggle:");
    } else {
        form_inline_append_text(dst, pos, cap, "form:edit:");
    }
    form_inline_append_u32(dst, pos, cap, control_index);
    form_inline_append_text(dst, pos, cap, "\">");
    if (control->kind == BROWSER_FORM_CONTROL_SUBMIT ||
        control->kind == BROWSER_FORM_CONTROL_RESET) {
        form_inline_append_text(dst, pos, cap, "[");
        form_inline_append_html_text(dst, pos, cap,
                                     control->label[0] ? control->label
                                                       : control->kind == BROWSER_FORM_CONTROL_RESET
                                                             ? "Reset"
                                                             : "Submit");
        form_inline_append_text(dst, pos, cap, "]");
    } else if (control->kind == BROWSER_FORM_CONTROL_CHECKBOX) {
        form_inline_append_text(dst, pos, cap,
                                (control->flags & BROWSER_FORM_CONTROL_CHECKED)
                                    ? "[x] "
                                    : "[ ] ");
        form_inline_append_html_text(dst, pos, cap,
                                     control->label[0] ? control->label
                                                       : control->name);
    } else if (control->kind == BROWSER_FORM_CONTROL_RADIO) {
        form_inline_append_text(dst, pos, cap,
                                (control->flags & BROWSER_FORM_CONTROL_CHECKED)
                                    ? "(o) "
                                    : "( ) ");
        form_inline_append_html_text(dst, pos, cap,
                                     control->label[0] ? control->label
                                                       : control->value);
    } else if (control->kind == BROWSER_FORM_CONTROL_SELECT) {
        form_inline_append_text(dst, pos, cap, "[");
        for (uint32_t i = 0; i < BROWSER_FORM_SELECT_CELLS; ++i) {
            form_inline_append_char(dst, pos, cap, '_');
        }
        form_inline_append_text(dst, pos, cap, "]");
    } else if (control->kind == BROWSER_FORM_CONTROL_TEXTAREA) {
        form_inline_append_text(dst, pos, cap, "[");
        for (uint32_t i = 0; i < BROWSER_FORM_TEXTAREA_CELLS; ++i) {
            form_inline_append_char(dst, pos, cap, '_');
        }
        form_inline_append_text(dst, pos, cap, "]");
    } else {
        form_inline_append_text(dst, pos, cap, "[");
        for (uint32_t i = 0; i < BROWSER_FORM_INPUT_CELLS; ++i) {
            form_inline_append_char(dst, pos, cap, '_');
        }
        form_inline_append_text(dst, pos, cap, "]");
    }
    form_inline_append_text(dst, pos, cap, "</a>");
}

static uint32_t form_find_closing_tag(const char *source, uint32_t start,
                                      const char *tag_name,
                                      uint32_t *close_start)
{
    uint32_t scan = start;
    if (close_start) {
        *close_start = start;
    }
    while (source && source[scan]) {
        char tag[256];
        char name[24];
        uint8_t closing = 0;
        uint32_t next;
        if (source[scan] != '<') {
            ++scan;
            continue;
        }
        next = form_copy_tag_at(source, scan, tag, sizeof(tag));
        if (!next || !form_tag_name(tag, name, sizeof(name), &closing)) {
            ++scan;
            continue;
        }
        if (closing && form_text_eq(name, tag_name)) {
            if (close_start) {
                *close_start = scan;
            }
            return next;
        }
        scan = next;
    }
    return start;
}

static uint32_t form_parse_button(const char *source, uint32_t content_start,
                                  const char *tag, int current_form)
{
    char type[24];
    char name[BROWSER_FORM_NAME_CAP];
    char label[BROWSER_FORM_LABEL_CAP];
    char value[BROWSER_FORM_VALUE_CAP];
    uint32_t flags = 0;
    if (current_form < 0) {
        return BROWSER_MAX_FORM_CONTROLS;
    }
    type[0] = 0;
    name[0] = 0;
    label[0] = 0;
    value[0] = 0;
    form_extract_attr(tag, "type", type, sizeof(type));
    if (type[0] && !form_text_eq_ignore_case(type, "submit") &&
        !form_text_eq_ignore_case(type, "reset")) {
        return BROWSER_MAX_FORM_CONTROLS;
    }
    form_extract_attr(tag, "name", name, sizeof(name));
    form_extract_attr(tag, "value", value, sizeof(value));
    if (form_has_attr(tag, "disabled")) {
        flags |= BROWSER_FORM_CONTROL_DISABLED;
    }
    form_capture_button_label(source, content_start, label, sizeof(label));
    if (!label[0]) {
        copy_text(label, sizeof(label),
                  value[0] ? value
                             : form_text_eq_ignore_case(type, "reset")
                                   ? "Reset"
                                   : "Submit");
    }
    return form_add_control(form_text_eq_ignore_case(type, "reset")
                                ? BROWSER_FORM_CONTROL_RESET
                                : BROWSER_FORM_CONTROL_SUBMIT,
                            current_form, name, value, label, flags);
}

static uint32_t form_find_closing_button(const char *source, uint32_t start)
{
    return form_find_closing_tag(source, start, "button", 0);
}

static uint32_t form_parse_textarea(const char *source, uint32_t content_start,
                                    uint32_t close_start, const char *tag,
                                    int current_form,
                                    const struct form_old_value *old_values,
                                    uint32_t old_count)
{
    char name[BROWSER_FORM_NAME_CAP];
    char value[BROWSER_FORM_VALUE_CAP];
    char label[BROWSER_FORM_LABEL_CAP];
    uint32_t flags = 0;
    uint32_t index;
    if (current_form < 0) {
        return BROWSER_MAX_FORM_CONTROLS;
    }
    name[0] = 0;
    value[0] = 0;
    label[0] = 0;
    form_extract_attr(tag, "name", name, sizeof(name));
    form_extract_attr(tag, "placeholder", label, sizeof(label));
    if (label[0]) {
        flags |= BROWSER_FORM_CONTROL_PLACEHOLDER;
    }
    if (form_has_attr(tag, "disabled")) {
        flags |= BROWSER_FORM_CONTROL_DISABLED;
    }
    if (form_has_attr(tag, "readonly")) {
        flags |= BROWSER_FORM_CONTROL_READONLY;
    }
    if (close_start > content_start) {
        form_capture_html_text(source, content_start, close_start,
                               value, sizeof(value));
    }
    index = form_add_control(BROWSER_FORM_CONTROL_TEXTAREA, current_form,
                             name, value, label[0] ? label : name, flags);
    if (index < BROWSER_MAX_FORM_CONTROLS) {
        form_restore_old_value(&browser_form_controls[index], index,
                               old_values, old_count);
    }
    return index;
}

static void form_parse_select_options(const char *source, uint32_t start,
                                      uint32_t end, uint32_t control_index)
{
    uint32_t scan = start;
    while (source && source[scan] && scan < end) {
        char tag[256];
        char name[24];
        char value[BROWSER_FORM_VALUE_CAP];
        char label[BROWSER_FORM_LABEL_CAP];
        uint8_t closing = 0;
        uint32_t next;
        uint32_t option_close_start = 0;
        uint32_t option_close_next;
        if (source[scan] != '<') {
            ++scan;
            continue;
        }
        next = form_copy_tag_at(source, scan, tag, sizeof(tag));
        if (!next || !form_tag_name(tag, name, sizeof(name), &closing)) {
            ++scan;
            continue;
        }
        if (closing || !form_text_eq(name, "option")) {
            scan = next;
            continue;
        }
        value[0] = 0;
        label[0] = 0;
        form_extract_attr(tag, "value", value, sizeof(value));
        option_close_next = form_find_closing_tag(source, next, "option",
                                                  &option_close_start);
        if (!option_close_next || option_close_start <= next ||
            option_close_start > end) {
            option_close_start = next;
            while (option_close_start < end && source[option_close_start] &&
                   source[option_close_start] != '<') {
                ++option_close_start;
            }
            option_close_next = option_close_start;
        }
        form_capture_html_text(source, next, option_close_start,
                               label, sizeof(label));
        if (!value[0]) {
            copy_text(value, sizeof(value), label);
        }
        (void)form_add_option(control_index, value, label,
                              (uint8_t)form_has_attr(tag, "selected"));
        scan = option_close_next > next ? option_close_next : next;
    }
}

static uint32_t form_parse_select(const char *source, uint32_t content_start,
                                  uint32_t close_start, const char *tag,
                                  int current_form,
                                  const struct form_old_value *old_values,
                                  uint32_t old_count)
{
    char name[BROWSER_FORM_NAME_CAP];
    char label[BROWSER_FORM_LABEL_CAP];
    uint32_t flags = 0;
    uint32_t index;
    if (current_form < 0) {
        return BROWSER_MAX_FORM_CONTROLS;
    }
    name[0] = 0;
    label[0] = 0;
    form_extract_attr(tag, "name", name, sizeof(name));
    form_extract_attr(tag, "placeholder", label, sizeof(label));
    if (form_has_attr(tag, "disabled")) {
        flags |= BROWSER_FORM_CONTROL_DISABLED;
    }
    index = form_add_control(BROWSER_FORM_CONTROL_SELECT, current_form,
                             name, "", label[0] ? label : name, flags);
    if (index >= BROWSER_MAX_FORM_CONTROLS) {
        return index;
    }
    form_parse_select_options(source, content_start, close_start, index);
    form_restore_old_value(&browser_form_controls[index], index,
                           old_values, old_count);
    form_select_sync_label(index);
    if (!browser_form_controls[index].label[0]) {
        copy_text(browser_form_controls[index].label,
                  sizeof(browser_form_controls[index].label),
                  browser_form_controls[index].value[0]
                      ? browser_form_controls[index].value
                      : name);
    }
    return index;
}

void browser_forms_clear(void)
{
    browser_form_count = 0;
    browser_form_control_count = 0;
    browser_form_option_count = 0;
}

static int form_control_is_editable(uint32_t control_index)
{
    struct browser_form_control *control;
    if (control_index >= browser_form_control_count) {
        return 0;
    }
    control = &browser_form_controls[control_index];
    return control->kind == BROWSER_FORM_CONTROL_TEXT ||
           control->kind == BROWSER_FORM_CONTROL_PASSWORD ||
           control->kind == BROWSER_FORM_CONTROL_TEXTAREA;
}

static int form_control_can_edit(uint32_t control_index)
{
    struct browser_form_control *control;
    if (!form_control_is_editable(control_index)) {
        return 0;
    }
    control = &browser_form_controls[control_index];
    return !(control->flags & (BROWSER_FORM_CONTROL_DISABLED |
                               BROWSER_FORM_CONTROL_READONLY));
}

void browser_form_clear_focus(void)
{
    browser_form_focus_active = 0;
    browser_form_focus_control = 0;
    browser_form_edit_state.focused = 0;
    browser_form_edit_state.selecting = 0;
}

void browser_form_rebind_focus(void)
{
    uint32_t cursor = browser_form_edit_state.cursor;
    uint32_t scroll = browser_form_edit_state.scroll;
    uint32_t anchor = browser_form_edit_state.selection_anchor;
    if (!browser_form_focus_active) {
        return;
    }
    if (!form_control_can_edit(browser_form_focus_control)) {
        browser_form_clear_focus();
        return;
    }
    leonos_ui_edit_state_init(&browser_form_edit_state,
                              browser_form_controls[browser_form_focus_control].value,
                              sizeof(browser_form_controls[browser_form_focus_control].value));
    if (cursor > browser_form_edit_state.length) {
        cursor = browser_form_edit_state.length;
    }
    if (scroll > browser_form_edit_state.length) {
        scroll = browser_form_edit_state.length;
    }
    if (anchor > browser_form_edit_state.length) {
        anchor = cursor;
    }
    browser_form_edit_state.cursor = cursor;
    browser_form_edit_state.scroll = scroll;
    browser_form_edit_state.selection_anchor = anchor;
    browser_form_edit_state.focused = 1;
}

static void form_focus_control(uint32_t control_index)
{
    if (!form_control_can_edit(control_index)) {
        return;
    }
    browser_form_focus_active = 1;
    browser_form_focus_control = control_index;
    leonos_ui_edit_state_init(&browser_form_edit_state,
                              browser_form_controls[control_index].value,
                              sizeof(browser_form_controls[control_index].value));
    browser_form_edit_state.focused = 1;
    address_edit.focused = 0;
    menu_open = BROWSER_MENU_NONE;
    set_status(T("Editing form input", "正在编辑表单输入"));
}

int browser_form_input_active(void)
{
    return browser_form_focus_active && form_control_can_edit(browser_form_focus_control);
}

const char *browser_forms_render_inline_source(const char *source,
                                               const char *base_url)
{
    struct form_old_value old_values[BROWSER_MAX_FORM_CONTROLS];
    uint32_t old_count = 0;
    uint32_t scan = 0;
    uint32_t out = 0;
    int current_form = -1;
    if (!source) {
        source = "";
    }
    if (form_skip_restore_once) {
        form_skip_restore_once = 0;
    } else {
        form_collect_old_values(old_values, &old_count);
    }
    browser_forms_clear();
    form_inline_source[0] = 0;
    while (source[scan]) {
        char tag[256];
        char name[24];
        uint8_t closing = 0;
        uint32_t tag_start = scan;
        uint32_t next;
        if (source[scan] != '<') {
            form_inline_append_char(form_inline_source, &out,
                                    sizeof(form_inline_source), source[scan]);
            ++scan;
            continue;
        }
        next = form_copy_tag_at(source, scan, tag, sizeof(tag));
        if (!next || !form_tag_name(tag, name, sizeof(name), &closing)) {
            form_inline_append_char(form_inline_source, &out,
                                    sizeof(form_inline_source), source[scan]);
            ++scan;
            continue;
        }
        if (form_text_eq(name, "form")) {
            if (closing) {
                current_form = -1;
            } else {
                (void)form_add_form(tag, base_url, &current_form);
            }
            scan = next;
        } else if (!closing && form_text_eq(name, "input")) {
            uint32_t control_index =
                form_parse_input(tag, current_form, old_values, old_count);
            form_inline_append_control(form_inline_source, &out,
                                       sizeof(form_inline_source),
                                       control_index);
            scan = next;
        } else if (!closing && form_text_eq(name, "textarea")) {
            uint32_t close_start = next;
            uint32_t close_next = form_find_closing_tag(source, next,
                                                        "textarea",
                                                        &close_start);
            uint32_t control_index =
                form_parse_textarea(source, next,
                                    close_next > next ? close_start : next,
                                    tag, current_form,
                                    old_values, old_count);
            form_inline_append_control(form_inline_source, &out,
                                       sizeof(form_inline_source),
                                       control_index);
            scan = close_next > next ? close_next : next;
        } else if (!closing && form_text_eq(name, "select")) {
            uint32_t close_start = next;
            uint32_t close_next = form_find_closing_tag(source, next,
                                                        "select",
                                                        &close_start);
            uint32_t control_index =
                form_parse_select(source, next,
                                  close_next > next ? close_start : next,
                                  tag, current_form,
                                  old_values, old_count);
            form_inline_append_control(form_inline_source, &out,
                                       sizeof(form_inline_source),
                                       control_index);
            scan = close_next > next ? close_next : next;
        } else if (!closing && form_text_eq(name, "button")) {
            uint32_t control_index = form_parse_button(source, next,
                                                       tag, current_form);
            if (control_index < BROWSER_MAX_FORM_CONTROLS) {
                uint32_t close_next = form_find_closing_button(source, next);
                form_inline_append_control(form_inline_source, &out,
                                           sizeof(form_inline_source),
                                           control_index);
                scan = close_next > next ? close_next : next;
            } else {
                form_inline_append_range(form_inline_source, &out,
                                         sizeof(form_inline_source),
                                         source + tag_start, next - tag_start);
                scan = next;
            }
        } else {
            form_inline_append_range(form_inline_source, &out,
                                     sizeof(form_inline_source),
                                     source + tag_start, next - tag_start);
            scan = next;
        }
    }
    return form_inline_source;
}

static uint32_t form_parse_u32(const char *text)
{
    uint32_t value = 0;
    while (text && *text >= '0' && *text <= '9') {
        value = value * 10U + (uint32_t)(*text - '0');
        ++text;
    }
    return value;
}

int browser_form_control_from_href(const char *href, uint32_t *control_index)
{
    uint32_t index;
    if (!href || !control_index) {
        return 0;
    }
    if (form_starts_with(href, "form:edit:")) {
        index = form_parse_u32(href + 10);
    } else if (form_starts_with(href, "form:submit:")) {
        index = form_parse_u32(href + 12);
    } else if (form_starts_with(href, "form:toggle:")) {
        index = form_parse_u32(href + 12);
    } else if (form_starts_with(href, "form:reset:")) {
        index = form_parse_u32(href + 11);
    } else {
        return 0;
    }
    if (index >= browser_form_control_count) {
        return 0;
    }
    *control_index = index;
    return 1;
}

int browser_form_line_has_control(const struct browser_line *line)
{
    if (!line) {
        return 0;
    }
    for (uint32_t i = 0; i < line->len; i = browser_line_next_byte(line, i)) {
        uint8_t link = line->link[i];
        uint32_t control_index;
        if (link && (uint32_t)(link - 1U) < link_count &&
            browser_form_control_from_href(links[link - 1U].href,
                                           &control_index)) {
            const struct browser_form_control *control =
                &browser_form_controls[control_index];
            if (control->kind != BROWSER_FORM_CONTROL_HIDDEN) {
                return 1;
            }
        }
    }
    return 0;
}

void browser_form_control_rect(uint32_t control_index,
                               struct leonos_ui_rect *rect)
{
    uint32_t px = text_x();
    uint32_t py = text_y();
    uint32_t page_bottom = page_y() + page_h() - 8U;
    uint32_t doc_w = document_text_w();
    uint32_t y = py;
    if (!rect) {
        return;
    }
    *rect = (struct leonos_ui_rect){0};
    for (uint32_t row = scroll_line; row < line_count && y < page_bottom; ++row) {
        struct browser_line *line = &lines[row];
        int32_t line_px = (int32_t)px - (int32_t)scroll_x +
                          (int32_t)((uint32_t)line->indent * LEONOS_FONT_W);
        uint32_t image_text_offset = line->kind == BROWSER_LINE_IMAGE ? 20U : 0U;
        uint32_t cell_w = browser_line_cell_w(line->kind);
        uint32_t line_h = browser_line_render_height(line);
        uint32_t start = 0;
        line_px += (int32_t)line_align_shift_px(line, doc_w);
        while (start < line->len) {
            uint8_t link = line->link[start];
            uint32_t end = browser_line_next_byte(line, start);
            while (end < line->len && line->link[end] == link) {
                end = browser_line_next_byte(line, end);
            }
            if (link && (uint32_t)(link - 1U) < link_count) {
                uint32_t index;
                if (browser_form_control_from_href(links[link - 1U].href, &index) &&
                    index == control_index) {
                    uint32_t start_cells = browser_line_cells_between(line, 0, start);
                    uint32_t run_cells = browser_line_cells_between(line, start, end);
                    int32_t control_x = line_px + (int32_t)(image_text_offset +
                                                            start_cells * cell_w);
                    int32_t control_right = control_x + (int32_t)(run_cells * cell_w);
                    uint32_t control_w = run_cells * cell_w;
                    if (control_right <= (int32_t)px ||
                        control_x >= (int32_t)(px + doc_w)) {
                        return;
                    }
                    if (control_x < (int32_t)px) {
                        uint32_t hidden = (uint32_t)((int32_t)px - control_x);
                        if (hidden >= control_w) {
                            return;
                        }
                        control_x = (int32_t)px;
                        control_w -= hidden;
                    }
                    if (control_x + (int32_t)control_w > (int32_t)(px + doc_w)) {
                        control_w = (uint32_t)((int32_t)(px + doc_w) - control_x);
                    }
                    rect->x = control_x;
                    rect->y = (int32_t)(y + (line_h > BROWSER_FORM_WIDGET_H
                                                 ? (line_h - BROWSER_FORM_WIDGET_H) / 2U
                                                 : 0U));
                    rect->w = control_w;
                    rect->h = BROWSER_FORM_WIDGET_H;
                    return;
                }
            }
            start = end;
        }
        y += line_h;
    }
}

static void form_draw_password_mask(const char *value, char *out, uint32_t cap)
{
    uint32_t len = value ? (uint32_t)strlen(value) : 0;
    uint32_t pos = 0;
    if (!out || cap == 0) {
        return;
    }
    while (len-- && pos + 1U < cap) {
        out[pos++] = '*';
    }
    out[pos] = 0;
}

static void form_edit_ensure_cursor_visible(uint32_t w)
{
    uint32_t cols = w > 8U ? (w - 8U) / LEONOS_FONT_W : 0;
    if (!browser_form_edit_state.buffer || cols == 0) {
        return;
    }
    leonos_ui_edit_state_sync(&browser_form_edit_state);
    if (browser_form_edit_state.cursor < browser_form_edit_state.scroll) {
        browser_form_edit_state.scroll = browser_form_edit_state.cursor;
    }
    while (browser_form_edit_state.cursor > browser_form_edit_state.scroll + cols &&
           browser_form_edit_state.scroll < browser_form_edit_state.cursor) {
        ++browser_form_edit_state.scroll;
    }
}

void browser_draw_form_control(uint32_t x, uint32_t y, uint32_t w,
                               uint32_t control_index)
{
    struct browser_form_control *control;
    char display[BROWSER_FORM_VALUE_CAP];
    uint32_t flags = 0;
    uint32_t cursor = 0;
    uint32_t scroll = 0;
    const char *text;
    uint8_t focused = 0;
    uint8_t show_placeholder = 0;
    if (control_index >= browser_form_control_count || w < 12U) {
        return;
    }
    control = &browser_form_controls[control_index];
    if (control->kind == BROWSER_FORM_CONTROL_SUBMIT ||
        control->kind == BROWSER_FORM_CONTROL_RESET) {
        leonos_ui_button(&ui, x, y, w, BROWSER_FORM_WIDGET_H,
                         control->label[0] ? control->label
                                           : control->kind == BROWSER_FORM_CONTROL_RESET
                                                 ? "Reset"
                                                 : "Submit",
                         (control->flags & BROWSER_FORM_CONTROL_DISABLED)
                             ? LEONOS_UI_BUTTON_DISABLED
                             : 0);
        return;
    }
    if (control->kind == BROWSER_FORM_CONTROL_CHECKBOX) {
        leonos_ui_checkbox(&ui, x, y,
                           control->label[0] ? control->label : control->name,
                           (control->flags & BROWSER_FORM_CONTROL_CHECKED) != 0,
                           (control->flags & BROWSER_FORM_CONTROL_DISABLED)
                               ? LEONOS_UI_BUTTON_DISABLED
                               : 0);
        return;
    }
    if (control->kind == BROWSER_FORM_CONTROL_RADIO) {
        leonos_ui_radio(&ui, x, y,
                        control->label[0] ? control->label : control->value,
                        (control->flags & BROWSER_FORM_CONTROL_CHECKED) != 0,
                        (control->flags & BROWSER_FORM_CONTROL_DISABLED)
                            ? LEONOS_UI_BUTTON_DISABLED
                            : 0);
        return;
    }
    if (control->kind == BROWSER_FORM_CONTROL_SELECT) {
        leonos_ui_combobox(&ui, x, y, w,
                           control->label[0] ? control->label : control->value,
                           0,
                           (control->flags & BROWSER_FORM_CONTROL_DISABLED)
                               ? LEONOS_UI_EDIT_DISABLED
                               : 0);
        return;
    }
    if (!form_control_is_editable(control_index)) {
        return;
    }
    if (browser_form_focus_active &&
        browser_form_focus_control == control_index) {
        focused = 1;
    }
    show_placeholder = !focused && !control->value[0] &&
                       (control->flags & BROWSER_FORM_CONTROL_PLACEHOLDER) &&
                       control->label[0];
    if (control->kind == BROWSER_FORM_CONTROL_PASSWORD) {
        form_draw_password_mask(control->value, display, sizeof(display));
        text = display;
    } else {
        text = control->value;
    }
    if (control->flags & BROWSER_FORM_CONTROL_DISABLED) {
        flags |= LEONOS_UI_EDIT_DISABLED;
    } else if (control->flags & BROWSER_FORM_CONTROL_READONLY) {
        flags |= LEONOS_UI_EDIT_READONLY;
    }
    if (focused) {
        form_edit_ensure_cursor_visible(w);
        flags |= LEONOS_UI_EDIT_FOCUSED;
        cursor = browser_form_edit_state.cursor;
        scroll = browser_form_edit_state.scroll;
    }
    leonos_ui_edit(&ui, x, y, w, text, cursor, scroll, flags);
    if (show_placeholder) {
        uint32_t bg = (flags & LEONOS_UI_EDIT_DISABLED)
                          ? LEONOS_UI_LIGHT
                          : LEONOS_UI_WHITE;
        leonos_ui_text_clipped(&ui, x + 4U, y + 4U,
                               w > 8U ? w - 8U : w,
                               control->label, LEONOS_UI_DARK, bg);
    }
}

static void form_url_encode(char *dst, uint32_t *pos, uint32_t cap, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    while (src && *src) {
        uint8_t ch = (uint8_t)*src++;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.') {
            append_char(dst, pos, cap, (char)ch);
        } else if (ch == ' ') {
            append_char(dst, pos, cap, '+');
        } else {
            append_char(dst, pos, cap, '%');
            append_char(dst, pos, cap, hex[ch >> 4]);
            append_char(dst, pos, cap, hex[ch & 0x0fU]);
        }
    }
}

static void form_append_pair(char *body, uint32_t *pos, uint32_t cap,
                             const char *name, const char *value)
{
    if (!name || !name[0]) {
        return;
    }
    if (*pos) {
        append_char(body, pos, cap, '&');
    }
    form_url_encode(body, pos, cap, name);
    append_char(body, pos, cap, '=');
    form_url_encode(body, pos, cap, value ? value : "");
}

static void form_build_body(uint32_t form_index, uint32_t submit_index,
                            char *body, uint32_t cap)
{
    const struct browser_form *form = &browser_forms[form_index];
    uint32_t start = form->first_control;
    uint32_t end = start + form->control_count;
    uint32_t pos = 0;
    body[0] = 0;
    for (uint32_t i = start; i < end && i < browser_form_control_count; ++i) {
        const struct browser_form_control *control = &browser_form_controls[i];
        if (control->flags & BROWSER_FORM_CONTROL_DISABLED) {
            continue;
        }
        if (control->kind == BROWSER_FORM_CONTROL_SUBMIT) {
            if (i == submit_index && control->name[0]) {
                form_append_pair(body, &pos, cap, control->name, control->value);
            }
            continue;
        }
        if (control->kind == BROWSER_FORM_CONTROL_RESET) {
            continue;
        }
        if ((control->kind == BROWSER_FORM_CONTROL_CHECKBOX ||
             control->kind == BROWSER_FORM_CONTROL_RADIO) &&
            !(control->flags & BROWSER_FORM_CONTROL_CHECKED)) {
            continue;
        }
        form_append_pair(body, &pos, cap, control->name, control->value);
    }
}

static int form_url_has_query(const char *url)
{
    while (url && *url && *url != '#') {
        if (*url == '?') {
            return 1;
        }
        ++url;
    }
    return 0;
}

static void form_submit(uint32_t control_index)
{
    struct browser_form_control *control;
    struct browser_form *form;
    char body[FORM_BODY_CAP];
    if (control_index >= browser_form_control_count) {
        return;
    }
    control = &browser_form_controls[control_index];
    if (control->kind != BROWSER_FORM_CONTROL_SUBMIT ||
        (control->flags & BROWSER_FORM_CONTROL_DISABLED) ||
        control->form_index >= browser_form_count) {
        return;
    }
    browser_form_clear_focus();
    form = &browser_forms[control->form_index];
    form_build_body(control->form_index, control_index, body, sizeof(body));
    if (form_text_eq_ignore_case(form->method, "post")) {
        load_http_form_post(form->action, body);
        push_history(current_location);
        return;
    }
    {
        char url[BROWSER_URL_CAP];
        uint32_t pos = 0;
        copy_text(url, sizeof(url), form->action);
        pos = (uint32_t)strlen(url);
        if (body[0]) {
            append_char(url, &pos, sizeof(url), form_url_has_query(form->action) ? '&' : '?');
            append_text(url, &pos, sizeof(url), body);
        }
        navigate_to(url, 1);
    }
}

static void form_toggle_control(uint32_t control_index)
{
    struct browser_form_control *control;
    if (control_index >= browser_form_control_count) {
        return;
    }
    control = &browser_form_controls[control_index];
    if (control->flags & BROWSER_FORM_CONTROL_DISABLED) {
        return;
    }
    if (control->kind == BROWSER_FORM_CONTROL_CHECKBOX) {
        control->flags ^= BROWSER_FORM_CONTROL_CHECKED;
        set_status((control->flags & BROWSER_FORM_CONTROL_CHECKED)
                       ? T("Checkbox checked", "复选框已选中")
                       : T("Checkbox unchecked", "复选框已取消"));
        return;
    }
    if (control->kind == BROWSER_FORM_CONTROL_RADIO) {
        for (uint32_t i = 0; i < browser_form_control_count; ++i) {
            struct browser_form_control *other = &browser_form_controls[i];
            if (other->kind == BROWSER_FORM_CONTROL_RADIO &&
                other->form_index == control->form_index &&
                form_text_eq(other->name, control->name)) {
                other->flags &= ~BROWSER_FORM_CONTROL_CHECKED;
            }
        }
        control->flags |= BROWSER_FORM_CONTROL_CHECKED;
        set_status(T("Radio option selected", "单选项已选择"));
        return;
    }
    if (control->kind == BROWSER_FORM_CONTROL_SELECT &&
        control->option_count &&
        control->first_option < BROWSER_MAX_FORM_OPTIONS) {
        uint32_t next = control->first_option;
        for (uint32_t i = 0; i < control->option_count; ++i) {
            uint32_t option_index = control->first_option + i;
            if (option_index >= browser_form_option_count) {
                break;
            }
            if (form_text_eq(browser_form_options[option_index].value,
                             control->value)) {
                next = control->first_option +
                       ((i + 1U) % control->option_count);
                break;
            }
        }
        if (next < browser_form_option_count) {
            copy_text(control->value, sizeof(control->value),
                      browser_form_options[next].value);
            copy_text(control->label, sizeof(control->label),
                      browser_form_options[next].label);
            set_status(T("Selection changed", "选项已更改"));
        }
    }
}

static void form_reset_page(void)
{
    browser_form_clear_focus();
    form_skip_restore_once = 1;
    rerender_page();
    set_status(T("Form reset", "表单已重置"));
}

static void form_edit(uint32_t control_index)
{
    form_focus_control(control_index);
}

static uint32_t form_find_submit_for_control(uint32_t control_index)
{
    const struct browser_form_control *control;
    struct browser_form *form;
    uint32_t start;
    uint32_t end;
    if (control_index >= browser_form_control_count) {
        return BROWSER_MAX_FORM_CONTROLS;
    }
    control = &browser_form_controls[control_index];
    if (control->form_index >= browser_form_count) {
        return BROWSER_MAX_FORM_CONTROLS;
    }
    form = &browser_forms[control->form_index];
    start = form->first_control;
    end = start + form->control_count;
    for (uint32_t i = start; i < end && i < browser_form_control_count; ++i) {
        if (browser_form_controls[i].kind == BROWSER_FORM_CONTROL_SUBMIT &&
            !(browser_form_controls[i].flags & BROWSER_FORM_CONTROL_DISABLED)) {
            return i;
        }
    }
    return BROWSER_MAX_FORM_CONTROLS;
}

static void form_focus_next_input(void)
{
    uint32_t start = browser_form_focus_active ? browser_form_focus_control + 1U : 0;
    for (uint32_t i = 0; i < browser_form_control_count; ++i) {
        uint32_t index = (start + i) % browser_form_control_count;
        if (form_control_can_edit(index)) {
            form_focus_control(index);
            return;
        }
    }
}

int browser_form_handle_click(const char *href, int32_t mx, int32_t my)
{
    struct leonos_ui_rect rect;
    uint32_t control_index;
    if (!href || !form_starts_with(href, "form:")) {
        return 0;
    }
    if (form_starts_with(href, "form:edit:")) {
        control_index = form_parse_u32(href + 10);
        form_edit(control_index);
        browser_form_control_rect(control_index, &rect);
        if (rect.w && rect.h) {
            (void)leonos_ui_edit_state_handle_mouse(&browser_form_edit_state,
                                                    mx, my,
                                                    (uint32_t)rect.x,
                                                    (uint32_t)rect.y,
                                                    rect.w, 1U);
        }
        return 1;
    }
    if (form_starts_with(href, "form:submit:")) {
        form_submit(form_parse_u32(href + 12));
        return 1;
    }
    if (form_starts_with(href, "form:toggle:")) {
        browser_form_clear_focus();
        form_toggle_control(form_parse_u32(href + 12));
        return 1;
    }
    if (form_starts_with(href, "form:reset:")) {
        control_index = form_parse_u32(href + 11);
        if (control_index < browser_form_control_count &&
            !(browser_form_controls[control_index].flags &
              BROWSER_FORM_CONTROL_DISABLED)) {
            form_reset_page();
        }
        return 1;
    }
    return 1;
}

int browser_form_handle_key(struct leonos_gui_app_event *event)
{
    uint32_t submit_index;
    if (!event || !browser_form_input_active()) {
        return 0;
    }
    if (!event->pressed) {
        (void)leonos_ui_edit_state_handle_key(&browser_form_edit_state,
                                              event->keycode, event->pressed);
        return 1;
    }
    if (event->keycode == 1U) {
        browser_form_clear_focus();
        return 1;
    }
    if (event->keycode == LEONOS_KEY_TAB) {
        form_focus_next_input();
        return 1;
    }
    if (event->keycode == LEONOS_KEY_ENTER) {
        submit_index = form_find_submit_for_control(browser_form_focus_control);
        if (submit_index < BROWSER_MAX_FORM_CONTROLS) {
            form_submit(submit_index);
        }
        return 1;
    }
    if (leonos_ui_edit_state_handle_key(&browser_form_edit_state,
                                        event->keycode,
                                        event->pressed)) {
        leonos_ui_edit_state_sync(&browser_form_edit_state);
        return 1;
    }
    return 1;
}
