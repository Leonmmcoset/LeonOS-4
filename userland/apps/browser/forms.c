#include "browser.h"

#define FORM_HREF_CAP 48U
#define FORM_BODY_CAP 768U

struct form_old_value {
    char name[BROWSER_FORM_NAME_CAP];
    char value[BROWSER_FORM_VALUE_CAP];
    uint8_t kind;
};

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

static void form_capture_button_label(const char *source, uint32_t start,
                                      char *out, uint32_t cap)
{
    uint32_t pos = start;
    uint32_t len = 0;
    char tmp[BROWSER_FORM_LABEL_CAP];
    if (out && cap) {
        out[0] = 0;
    }
    while (source && source[pos] && source[pos] != '<' && len + 1U < sizeof(tmp)) {
        tmp[len++] = source[pos++];
    }
    tmp[len] = 0;
    form_trim_copy(out, cap, tmp, len);
}

static void form_collect_old_values(struct form_old_value *old_values,
                                    uint32_t *old_count)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < browser_form_control_count &&
                         count < BROWSER_MAX_FORM_CONTROLS; ++i) {
        const struct browser_form_control *control = &browser_form_controls[i];
        if ((control->kind == BROWSER_FORM_CONTROL_TEXT ||
             control->kind == BROWSER_FORM_CONTROL_PASSWORD) &&
            control->name[0]) {
            old_values[count].kind = control->kind;
            copy_text(old_values[count].name, sizeof(old_values[count].name), control->name);
            copy_text(old_values[count].value, sizeof(old_values[count].value), control->value);
            ++count;
        }
    }
    *old_count = count;
}

static void form_restore_old_value(struct browser_form_control *control,
                                   const struct form_old_value *old_values,
                                   uint32_t old_count)
{
    if (!control || !control->name[0] ||
        !(control->kind == BROWSER_FORM_CONTROL_TEXT ||
          control->kind == BROWSER_FORM_CONTROL_PASSWORD)) {
        return;
    }
    for (uint32_t i = 0; i < old_count; ++i) {
        if (old_values[i].kind == control->kind &&
            form_text_eq(old_values[i].name, control->name)) {
            copy_text(control->value, sizeof(control->value), old_values[i].value);
            return;
        }
    }
}

static int form_add_form(const char *tag, int *current_form)
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
        leonos_http_resolve_url(current_location, action,
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
                                 const char *label)
{
    uint32_t index;
    if (form_index < 0 || browser_form_control_count >= BROWSER_MAX_FORM_CONTROLS) {
        return BROWSER_MAX_FORM_CONTROLS;
    }
    index = browser_form_control_count++;
    browser_form_controls[index] = (struct browser_form_control){0};
    browser_form_controls[index].kind = kind;
    browser_form_controls[index].form_index = (uint8_t)form_index;
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

static void form_parse_input(const char *tag, int current_form,
                             const struct form_old_value *old_values,
                             uint32_t old_count)
{
    char type[24];
    char name[BROWSER_FORM_NAME_CAP];
    char value[BROWSER_FORM_VALUE_CAP];
    char label[BROWSER_FORM_LABEL_CAP];
    uint8_t kind = BROWSER_FORM_CONTROL_TEXT;
    uint32_t index;
    if (current_form < 0) {
        return;
    }
    type[0] = 0;
    name[0] = 0;
    value[0] = 0;
    label[0] = 0;
    form_extract_attr(tag, "type", type, sizeof(type));
    form_extract_attr(tag, "name", name, sizeof(name));
    form_extract_attr(tag, "value", value, sizeof(value));
    form_extract_attr(tag, "placeholder", label, sizeof(label));
    if (!type[0] || form_text_eq_ignore_case(type, "text") ||
        form_text_eq_ignore_case(type, "email")) {
        kind = BROWSER_FORM_CONTROL_TEXT;
    } else if (form_text_eq_ignore_case(type, "password")) {
        kind = BROWSER_FORM_CONTROL_PASSWORD;
    } else if (form_text_eq_ignore_case(type, "hidden")) {
        kind = BROWSER_FORM_CONTROL_HIDDEN;
    } else if (form_text_eq_ignore_case(type, "submit")) {
        kind = BROWSER_FORM_CONTROL_SUBMIT;
        if (!label[0]) {
            copy_text(label, sizeof(label), value[0] ? value : "Submit");
        }
    } else {
        return;
    }
    index = form_add_control(kind, current_form, name, value,
                             label[0] ? label : name);
    if (index < BROWSER_MAX_FORM_CONTROLS) {
        form_restore_old_value(&browser_form_controls[index], old_values, old_count);
    }
}

static uint8_t form_add_link(const char *href)
{
    uint32_t index;
    if (!href || !href[0] || link_count >= BROWSER_MAX_LINKS) {
        return 0;
    }
    index = link_count++;
    copy_text(links[index].href, sizeof(links[index].href), href);
    return (uint8_t)(index + 1U);
}

static struct browser_line *form_new_line(void)
{
    struct browser_line *line;
    if (line_count >= BROWSER_MAX_LINES) {
        source_truncated = 1;
        return 0;
    }
    line = &lines[line_count++];
    *line = (struct browser_line){0};
    line->kind = BROWSER_LINE_NORMAL;
    line->line_bg = BROWSER_COLOR_UNSET;
    line->border_color = BROWSER_COLOR_UNSET;
    for (uint32_t i = 0; i < BROWSER_LINE_CHARS; ++i) {
        line->fg[i] = BROWSER_COLOR_UNSET;
        line->bg[i] = BROWSER_COLOR_UNSET;
    }
    return line;
}

static void form_line_append(struct browser_line *line, const char *text,
                             uint8_t link)
{
    while (line && text && *text && line->len + 1U < BROWSER_LINE_CHARS) {
        uint32_t idx = line->len++;
        line->text[idx] = *text++;
        line->text[line->len] = 0;
        line->link[idx] = link;
        line->style[idx] = 0;
        line->fg[idx] = BROWSER_COLOR_UNSET;
        line->bg[idx] = BROWSER_COLOR_UNSET;
        line->cell_width[idx] = 1;
        ++line->cells;
    }
}

static void form_display_value(const struct browser_form_control *control,
                               char *out, uint32_t cap)
{
    uint32_t pos = 0;
    out[0] = 0;
    if (control->kind == BROWSER_FORM_CONTROL_PASSWORD) {
        uint32_t len = (uint32_t)strlen(control->value);
        while (len-- && pos + 1U < cap) {
            out[pos++] = '*';
        }
        out[pos] = 0;
        return;
    }
    copy_text(out, cap, control->value[0] ? control->value : " ");
}

static void form_append_control_line(uint32_t control_index)
{
    const struct browser_form_control *control = &browser_form_controls[control_index];
    struct browser_line *line;
    char href[FORM_HREF_CAP];
    char value[BROWSER_FORM_VALUE_CAP];
    uint32_t pos = 0;
    uint8_t link;
    if (control->kind == BROWSER_FORM_CONTROL_HIDDEN) {
        return;
    }
    line = form_new_line();
    if (!line) {
        return;
    }
    href[0] = 0;
    append_text(href, &pos, sizeof(href),
                control->kind == BROWSER_FORM_CONTROL_SUBMIT ? "form:submit:" : "form:edit:");
    append_u32(href, &pos, sizeof(href), control_index);
    link = form_add_link(href);
    if (control->kind == BROWSER_FORM_CONTROL_SUBMIT) {
        form_line_append(line, "[", link);
        form_line_append(line, control->label[0] ? control->label : "Submit", link);
        form_line_append(line, "]", link);
        return;
    }
    form_line_append(line, control->label[0] ? control->label : control->name, 0);
    form_line_append(line, ": ", 0);
    form_display_value(control, value, sizeof(value));
    form_line_append(line, "[", link);
    form_line_append(line, value, link);
    form_line_append(line, "]", link);
}

static void form_append_lines(void)
{
    if (browser_form_control_count == 0) {
        return;
    }
    if (line_count > 0 && lines[line_count - 1U].len) {
        (void)form_new_line();
    }
    for (uint32_t i = 0; i < browser_form_control_count; ++i) {
        form_append_control_line(i);
    }
    clamp_scroll();
}

void browser_forms_clear(void)
{
    browser_form_count = 0;
    browser_form_control_count = 0;
}

void browser_forms_refresh(void)
{
    struct form_old_value old_values[BROWSER_MAX_FORM_CONTROLS];
    uint32_t old_count = 0;
    uint32_t scan = 0;
    int current_form = -1;
    if (!page_is_html) {
        browser_forms_clear();
        return;
    }
    form_collect_old_values(old_values, &old_count);
    browser_forms_clear();
    while (page_source[scan]) {
        char tag[256];
        char name[24];
        uint8_t closing = 0;
        uint32_t next;
        if (page_source[scan] != '<') {
            ++scan;
            continue;
        }
        next = form_copy_tag_at(page_source, scan, tag, sizeof(tag));
        if (!next || !form_tag_name(tag, name, sizeof(name), &closing)) {
            ++scan;
            continue;
        }
        if (form_text_eq(name, "form")) {
            if (closing) {
                current_form = -1;
            } else {
                (void)form_add_form(tag, &current_form);
            }
        } else if (!closing && form_text_eq(name, "input")) {
            form_parse_input(tag, current_form, old_values, old_count);
        } else if (!closing && form_text_eq(name, "button")) {
            char type[24];
            char label[BROWSER_FORM_LABEL_CAP];
            char value[BROWSER_FORM_VALUE_CAP];
            type[0] = 0;
            label[0] = 0;
            value[0] = 0;
            form_extract_attr(tag, "type", type, sizeof(type));
            if (!type[0] || form_text_eq_ignore_case(type, "submit")) {
                form_extract_attr(tag, "value", value, sizeof(value));
                form_capture_button_label(page_source, next, label, sizeof(label));
                if (!label[0]) {
                    copy_text(label, sizeof(label), value[0] ? value : "Submit");
                }
                (void)form_add_control(BROWSER_FORM_CONTROL_SUBMIT, current_form,
                                       "", value, label);
            }
        }
        scan = next;
    }
    form_append_lines();
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
        if (control->kind == BROWSER_FORM_CONTROL_SUBMIT) {
            if (i == submit_index && control->name[0]) {
                form_append_pair(body, &pos, cap, control->name, control->value);
            }
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
        control->form_index >= browser_form_count) {
        return;
    }
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

static void form_edit(uint32_t control_index)
{
    struct browser_form_control *control;
    char value[BROWSER_FORM_VALUE_CAP];
    if (control_index >= browser_form_control_count) {
        return;
    }
    control = &browser_form_controls[control_index];
    if (control->kind != BROWSER_FORM_CONTROL_TEXT &&
        control->kind != BROWSER_FORM_CONTROL_PASSWORD) {
        return;
    }
    if (browser_embedded) {
        browser_embed_start_form_edit(control_index);
        return;
    }
    copy_text(value, sizeof(value), control->value);
    if (leonos_ui_show_input_dialog(T("Form input", "表单输入"),
                                    control->label[0] ? control->label : control->name,
                                    value, sizeof(value))) {
        copy_text(control->value, sizeof(control->value), value);
        rerender_page();
        present_browser();
    }
}

int browser_form_handle_href(const char *href)
{
    if (!href || !form_starts_with(href, "form:")) {
        return 0;
    }
    if (form_starts_with(href, "form:edit:")) {
        form_edit(form_parse_u32(href + 10));
        return 1;
    }
    if (form_starts_with(href, "form:submit:")) {
        form_submit(form_parse_u32(href + 12));
        return 1;
    }
    return 1;
}
