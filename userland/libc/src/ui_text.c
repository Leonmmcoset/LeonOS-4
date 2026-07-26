#include <leonos/syscall.h>
#include <stdlib.h>

#include "ui_internal.h"

#define UI_TTF_METRO_PATH "0:/system/fonts/leonos-metro.ttf"
#define UI_TTF_WIN95_PATH "0:/system/fonts/leonos-win95.ttf"
#define UI_TTF_MAX (12U * 1024U * 1024U)
#define UI_TTF_POINTS_MAX 2048U
#define UI_TTF_EDGES_MAX 8192U

struct ui_ttf_font {
    uint8_t *data;
    uint32_t len;
    uint32_t cmap;
    uint32_t cmap_len;
    uint32_t glyf;
    uint32_t loca;
    uint32_t hmtx;
    uint16_t units_per_em;
    int16_t ascender;
    int16_t descender;
    uint16_t glyph_count;
    uint16_t hmetrics_count;
    uint8_t long_loca;
};

struct ui_ttf_point {
    int32_t x;
    int32_t y;
    uint8_t on_curve;
};

struct ui_ttf_edge {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
};

static struct ui_ttf_font ui_ttf;
static struct ui_ttf_point ui_ttf_scratch_points[UI_TTF_POINTS_MAX];
static uint16_t ui_ttf_scratch_contours[UI_TTF_POINTS_MAX];
static struct ui_ttf_point ui_ttf_points[UI_TTF_POINTS_MAX];
static uint16_t ui_ttf_contours[UI_TTF_POINTS_MAX];
static uint8_t ui_ttf_flags[UI_TTF_POINTS_MAX];
static struct ui_ttf_edge ui_ttf_edges[UI_TTF_EDGES_MAX];
static uint8_t ui_ttf_checked;
static uint8_t ui_ttf_metro;

static const char *ui_ttf_path(void)
{
    return ui_theme_is_metro() ? UI_TTF_METRO_PATH : UI_TTF_WIN95_PATH;
}

static uint16_t ui_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static int16_t ui_be16s(const uint8_t *p)
{
    return (int16_t)ui_be16(p);
}

static uint32_t ui_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int ui_ttf_range(uint32_t offset, uint32_t length)
{
    return offset <= ui_ttf.len && length <= ui_ttf.len - offset;
}

static uint32_t ui_ttf_table(const char tag[4], uint32_t *length)
{
    uint16_t count;
    if (!ui_ttf.data || ui_ttf.len < 12) {
        return 0;
    }
    count = ui_be16(ui_ttf.data + 4);
    if (!ui_ttf_range(12, (uint32_t)count * 16U)) {
        return 0;
    }
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t *record = ui_ttf.data + 12U + (uint32_t)i * 16U;
        if (record[0] == (uint8_t)tag[0] && record[1] == (uint8_t)tag[1] &&
            record[2] == (uint8_t)tag[2] && record[3] == (uint8_t)tag[3]) {
            uint32_t offset = ui_be32(record + 8);
            uint32_t size = ui_be32(record + 12);
            if (ui_ttf_range(offset, size)) {
                if (length) {
                    *length = size;
                }
                return offset;
            }
        }
    }
    return 0;
}

static int ui_ttf_read_file(const char *path)
{
    struct leonos_stat st;
    int fd;
    uint32_t length = 0;
    if (stat(path, &st) != 0 || st.type != LEONOS_FS_TYPE_FILE ||
        st.size < 12 || st.size > UI_TTF_MAX) {
        return 0;
    }
    ui_ttf.data = malloc((size_t)st.size);
    if (!ui_ttf.data) {
        return 0;
    }
    fd = open(path, LEONOS_O_RDONLY, 0);
    if (fd < 0) {
        free(ui_ttf.data);
        ui_ttf.data = 0;
        return 0;
    }
    while (length < st.size) {
        long got = read(fd, ui_ttf.data + length, (size_t)st.size - length);
        if (got <= 0) {
            break;
        }
        length += (uint32_t)got;
    }
    close(fd);
    ui_ttf.len = length;
    return length == st.size;
}

static void ui_ttf_load(void)
{
    uint32_t head_len, hhea_len, maxp_len, cmap_len, loca_len, glyf_len, hmtx_len;
    uint32_t head, hhea, maxp;
    uint8_t metro = (uint8_t)ui_theme_is_metro();
    if (ui_ttf_checked && ui_ttf_metro == metro) {
        return;
    }
    if (ui_ttf.data) {
        free(ui_ttf.data);
        ui_ttf = (struct ui_ttf_font){0};
    }
    ui_ttf_checked = 1;
    ui_ttf_metro = metro;
    if (!ui_ttf_read_file(ui_ttf_path())) {
        return;
    }
    head = ui_ttf_table("head", &head_len);
    hhea = ui_ttf_table("hhea", &hhea_len);
    maxp = ui_ttf_table("maxp", &maxp_len);
    ui_ttf.cmap = ui_ttf_table("cmap", &cmap_len);
    ui_ttf.loca = ui_ttf_table("loca", &loca_len);
    ui_ttf.glyf = ui_ttf_table("glyf", &glyf_len);
    ui_ttf.hmtx = ui_ttf_table("hmtx", &hmtx_len);
    if (!head || !hhea || !maxp || !ui_ttf.cmap || !ui_ttf.loca || !ui_ttf.glyf || !ui_ttf.hmtx ||
        head_len < 54 || hhea_len < 36 || maxp_len < 6 || cmap_len < 4 ||
        ui_be32(ui_ttf.data + head) != 0x00010000U) {
        free(ui_ttf.data);
        ui_ttf.data = 0;
        return;
    }
    ui_ttf.units_per_em = ui_be16(ui_ttf.data + head + 18);
    ui_ttf.long_loca = ui_be16(ui_ttf.data + head + 50) != 0;
    ui_ttf.ascender = ui_be16s(ui_ttf.data + hhea + 4);
    ui_ttf.descender = ui_be16s(ui_ttf.data + hhea + 6);
    ui_ttf.hmetrics_count = ui_be16(ui_ttf.data + hhea + 34);
    ui_ttf.glyph_count = ui_be16(ui_ttf.data + maxp + 4);
    ui_ttf.cmap_len = cmap_len;
    if (!ui_ttf.units_per_em || ui_ttf.ascender <= ui_ttf.descender ||
        !ui_ttf.glyph_count || !ui_ttf.hmetrics_count) {
        free(ui_ttf.data);
        ui_ttf.data = 0;
    }
}

static uint16_t ui_ttf_glyph(uint32_t codepoint)
{
    uint16_t tables;
    uint32_t selected = 0;
    uint16_t selected_format = 0;
    ui_ttf_load();
    if (!ui_ttf.data || codepoint > 0xffffU || ui_ttf.cmap_len < 4) {
        return 0;
    }
    tables = ui_be16(ui_ttf.data + ui_ttf.cmap + 2);
    if (4U + (uint32_t)tables * 8U > ui_ttf.cmap_len) {
        return 0;
    }
    for (uint16_t i = 0; i < tables; ++i) {
        const uint8_t *record = ui_ttf.data + ui_ttf.cmap + 4U + (uint32_t)i * 8U;
        uint32_t offset = ui_be32(record + 4);
        uint32_t pos = ui_ttf.cmap + offset;
        uint16_t format;
        if (offset >= ui_ttf.cmap_len || !ui_ttf_range(pos, 8)) {
            continue;
        }
        format = ui_be16(ui_ttf.data + pos);
        if (ui_be16(record) == 3 && ui_be16(record + 2) == 10 && format == 12) {
            selected = pos;
            selected_format = format;
            break;
        }
        if (!selected && ui_be16(record) == 3 && ui_be16(record + 2) == 1 && format == 4) {
            selected = pos;
            selected_format = format;
        }
    }
    if (!selected) {
        return 0;
    }
    if (selected_format == 12) {
        uint32_t length = ui_be32(ui_ttf.data + selected + 4);
        uint32_t groups = ui_be32(ui_ttf.data + selected + 12);
        uint32_t lo = 0;
        uint32_t hi = groups;
        if (length < 16 || !ui_ttf_range(selected, length) || groups > (length - 16U) / 12U) return 0;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2U;
            const uint8_t *group = ui_ttf.data + selected + 16U + mid * 12U;
            uint32_t start = ui_be32(group);
            uint32_t end = ui_be32(group + 4);
            if (codepoint < start) hi = mid;
            else if (codepoint > end) lo = mid + 1U;
            else return (uint16_t)(ui_be32(group + 8) + codepoint - start);
        }
        return 0;
    }
    {
        const uint8_t *map = ui_ttf.data + selected;
        uint16_t length = ui_be16(map + 2);
        uint16_t segments = ui_be16(map + 6) / 2U;
        uint32_t end_offset = selected + 14;
        uint32_t start_offset = end_offset + (uint32_t)segments * 2U + 2U;
        uint32_t delta_offset = start_offset + (uint32_t)segments * 2U;
        uint32_t range_offset = delta_offset + (uint32_t)segments * 2U;
        if (length < 16 || !ui_ttf_range(selected, length) ||
            range_offset + (uint32_t)segments * 2U > selected + length) {
            return 0;
        }
        for (uint16_t i = 0; i < segments; ++i) {
            uint16_t end = ui_be16(ui_ttf.data + end_offset + (uint32_t)i * 2U);
            uint16_t start = ui_be16(ui_ttf.data + start_offset + (uint32_t)i * 2U);
            uint16_t delta = ui_be16(ui_ttf.data + delta_offset + (uint32_t)i * 2U);
            uint16_t range = ui_be16(ui_ttf.data + range_offset + (uint32_t)i * 2U);
            if (codepoint > end) {
                continue;
            }
            if (codepoint < start) {
                return 0;
            }
            if (!range) {
                return (uint16_t)(codepoint + delta);
            }
            {
                uint32_t glyph_pos = range_offset + (uint32_t)i * 2U + range +
                                     (codepoint - start) * 2U;
                if (!ui_ttf_range(glyph_pos, 2) || glyph_pos + 2U > selected + length) {
                    return 0;
                }
                {
                    uint16_t glyph = ui_be16(ui_ttf.data + glyph_pos);
                    return glyph ? (uint16_t)(glyph + delta) : 0;
                }
            }
        }
    }
    return 0;
}

static int ui_ttf_glyph_range(uint16_t glyph, uint32_t *offset, uint32_t *length)
{
    uint32_t first, last;
    uint32_t entry = ui_ttf.loca + (uint32_t)glyph * (ui_ttf.long_loca ? 4U : 2U);
    if (!ui_ttf.data || glyph >= ui_ttf.glyph_count || !ui_ttf_range(entry, ui_ttf.long_loca ? 8 : 4)) {
        return 0;
    }
    first = ui_ttf.long_loca ? ui_be32(ui_ttf.data + entry) : (uint32_t)ui_be16(ui_ttf.data + entry) * 2U;
    last = ui_ttf.long_loca ? ui_be32(ui_ttf.data + entry + 4) : (uint32_t)ui_be16(ui_ttf.data + entry + 2) * 2U;
    if (last < first || !ui_ttf_range(ui_ttf.glyf + first, last - first)) {
        return 0;
    }
    *offset = ui_ttf.glyf + first;
    *length = last - first;
    return 1;
}

static uint16_t ui_ttf_advance(uint16_t glyph, int16_t *bearing)
{
    uint32_t pos;
    uint16_t advance;
    if (glyph >= ui_ttf.glyph_count) {
        return 0;
    }
    if (glyph < ui_ttf.hmetrics_count) {
        pos = ui_ttf.hmtx + (uint32_t)glyph * 4U;
        if (!ui_ttf_range(pos, 4)) {
            return 0;
        }
        advance = ui_be16(ui_ttf.data + pos);
        *bearing = ui_be16s(ui_ttf.data + pos + 2);
        return advance;
    }
    pos = ui_ttf.hmtx + (uint32_t)(ui_ttf.hmetrics_count - 1U) * 4U;
    if (!ui_ttf_range(pos, 4)) {
        return 0;
    }
    advance = ui_be16(ui_ttf.data + pos);
    pos = ui_ttf.hmtx + (uint32_t)ui_ttf.hmetrics_count * 4U +
          (uint32_t)(glyph - ui_ttf.hmetrics_count) * 2U;
    if (!ui_ttf_range(pos, 2)) {
        return 0;
    }
    *bearing = ui_be16s(ui_ttf.data + pos);
    return advance;
}

static uint32_t ui_ttf_pixel_width(uint32_t codepoint, uint32_t height,
                                   uint32_t fallback)
{
    uint16_t glyph;
    uint16_t advance;
    int16_t bearing;
    int32_t line_height;
    uint64_t scaled;
    if (!height) {
        return 0;
    }
    if (codepoint == '\n' || codepoint == '\r') {
        return 0;
    }
    if (codepoint == '\t') {
        uint32_t space = ui_ttf_pixel_width(' ', height, fallback);
        return space <= UINT32_MAX / 4U ? space * 4U : UINT32_MAX;
    }
    glyph = ui_ttf_glyph(codepoint);
    if (!glyph && codepoint != '?') {
        glyph = ui_ttf_glyph('?');
    }
    if (!glyph || !ui_ttf.data) {
        return fallback;
    }
    advance = ui_ttf_advance(glyph, &bearing);
    line_height = ui_ttf.ascender - ui_ttf.descender;
    if (!advance || line_height <= 0) {
        return fallback;
    }
    scaled = ((uint64_t)advance * height + (uint32_t)line_height / 2U) /
             (uint32_t)line_height;
    return scaled ? (uint32_t)scaled : 1U;
}

static int ui_ttf_simple_points(uint16_t glyph, uint16_t *point_count, uint16_t *contour_count)
{
    uint32_t offset, length, pos;
    int16_t contours;
    uint16_t count;
    int32_t coordinate;
    if (!ui_ttf_glyph_range(glyph, &offset, &length) || length < 10) {
        return 0;
    }
    contours = ui_be16s(ui_ttf.data + offset);
    if (contours < 0 || (uint16_t)contours > UI_TTF_POINTS_MAX) {
        return 0;
    }
    if (contours == 0) {
        *point_count = 0;
        *contour_count = 0;
        return 1;
    }
    pos = offset + 10;
    if (!ui_ttf_range(pos, (uint32_t)contours * 2U + 2U) || pos + (uint32_t)contours * 2U + 2U > offset + length) {
        return 0;
    }
    for (uint16_t i = 0; i < (uint16_t)contours; ++i) {
        ui_ttf_scratch_contours[i] = ui_be16(ui_ttf.data + pos + (uint32_t)i * 2U);
    }
    count = (uint16_t)(ui_ttf_scratch_contours[contours - 1] + 1U);
    if (!count || count > UI_TTF_POINTS_MAX) {
        return 0;
    }
    pos += (uint32_t)contours * 2U;
    {
        uint16_t instructions = ui_be16(ui_ttf.data + pos);
        pos += 2U + instructions;
    }
    if (pos > offset + length) {
        return 0;
    }
    for (uint16_t i = 0; i < count;) {
        uint8_t flag;
        if (pos >= offset + length) {
            return 0;
        }
        flag = ui_ttf.data[pos++];
        ui_ttf_flags[i++] = flag;
        if (flag & 0x08U) {
            uint8_t repeat;
            if (pos >= offset + length) {
                return 0;
            }
            repeat = ui_ttf.data[pos++];
            if ((uint32_t)i + repeat > count) {
                return 0;
            }
            while (repeat--) {
                ui_ttf_flags[i++] = flag;
            }
        }
    }
    coordinate = 0;
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t flag = ui_ttf_flags[i];
        if (flag & 0x02U) {
            if (pos >= offset + length) return 0;
            coordinate += (flag & 0x10U) ? ui_ttf.data[pos] : -(int32_t)ui_ttf.data[pos];
            ++pos;
        } else if (!(flag & 0x10U)) {
            if (pos + 2U > offset + length) return 0;
            coordinate += ui_be16s(ui_ttf.data + pos);
            pos += 2U;
        }
        ui_ttf_scratch_points[i].x = coordinate;
        ui_ttf_scratch_points[i].on_curve = (flag & 0x01U) != 0;
    }
    coordinate = 0;
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t flag = ui_ttf_flags[i];
        if (flag & 0x04U) {
            if (pos >= offset + length) return 0;
            coordinate += (flag & 0x20U) ? ui_ttf.data[pos] : -(int32_t)ui_ttf.data[pos];
            ++pos;
        } else if (!(flag & 0x20U)) {
            if (pos + 2U > offset + length) return 0;
            coordinate += ui_be16s(ui_ttf.data + pos);
            pos += 2U;
        }
        ui_ttf_scratch_points[i].y = coordinate;
    }
    *point_count = count;
    *contour_count = (uint16_t)contours;
    return 1;
}

static int ui_ttf_append_glyph(uint16_t glyph, int32_t a, int32_t b, int32_t c, int32_t d,
                               int32_t tx, int32_t ty, uint16_t depth,
                               uint16_t *point_count, uint16_t *contour_count)
{
    uint32_t offset, length;
    int16_t contours;
    if (depth > 8 || !ui_ttf_glyph_range(glyph, &offset, &length) || length < 10) return 0;
    contours = ui_be16s(ui_ttf.data + offset);
    if (contours >= 0) {
        uint16_t points, contour_total;
        uint16_t base = *point_count;
        if (!ui_ttf_simple_points(glyph, &points, &contour_total) ||
            points > UI_TTF_POINTS_MAX - base || contour_total > UI_TTF_POINTS_MAX - *contour_count) return 0;
        for (uint16_t index = 0; index < points; ++index) {
            int32_t source_x = ui_ttf_scratch_points[index].x;
            int32_t source_y = ui_ttf_scratch_points[index].y;
            ui_ttf_points[base + index].x = (int32_t)(((int64_t)a * source_x + (int64_t)b * source_y) >> 14) + tx;
            ui_ttf_points[base + index].y = (int32_t)(((int64_t)c * source_x + (int64_t)d * source_y) >> 14) + ty;
            ui_ttf_points[base + index].on_curve = ui_ttf_scratch_points[index].on_curve;
        }
        for (uint16_t index = 0; index < contour_total; ++index)
            ui_ttf_contours[*contour_count + index] = (uint16_t)(base + ui_ttf_scratch_contours[index]);
        *point_count = (uint16_t)(base + points);
        *contour_count = (uint16_t)(*contour_count + contour_total);
        return 1;
    }
    if (contours != -1) return 0;
    {
        uint32_t pos = offset + 10;
        uint16_t flags;
        do {
            uint16_t component;
            int32_t arg_x = 0, arg_y = 0;
            int32_t ca = 16384, cb = 0, cc = 0, cd = 16384;
            if (pos + 4U > offset + length) return 0;
            flags = ui_be16(ui_ttf.data + pos);
            component = ui_be16(ui_ttf.data + pos + 2);
            pos += 4U;
            if (flags & 0x0001U) {
                if (pos + 4U > offset + length) return 0;
                arg_x = ui_be16s(ui_ttf.data + pos);
                arg_y = ui_be16s(ui_ttf.data + pos + 2);
                pos += 4U;
            } else {
                if (pos + 2U > offset + length) return 0;
                arg_x = (int8_t)ui_ttf.data[pos];
                arg_y = (int8_t)ui_ttf.data[pos + 1];
                pos += 2U;
            }
            if (!(flags & 0x0002U)) return 0;
            if (flags & 0x0008U) {
                if (pos + 2U > offset + length) return 0;
                ca = cd = ui_be16s(ui_ttf.data + pos);
                pos += 2U;
            } else if (flags & 0x0040U) {
                if (pos + 4U > offset + length) return 0;
                ca = ui_be16s(ui_ttf.data + pos);
                cd = ui_be16s(ui_ttf.data + pos + 2);
                pos += 4U;
            } else if (flags & 0x0080U) {
                if (pos + 8U > offset + length) return 0;
                ca = ui_be16s(ui_ttf.data + pos);
                cb = ui_be16s(ui_ttf.data + pos + 2);
                cc = ui_be16s(ui_ttf.data + pos + 4);
                cd = ui_be16s(ui_ttf.data + pos + 6);
                pos += 8U;
            }
            if (!ui_ttf_append_glyph(component,
                                     (int32_t)(((int64_t)a * ca + (int64_t)b * cc) >> 14),
                                     (int32_t)(((int64_t)a * cb + (int64_t)b * cd) >> 14),
                                     (int32_t)(((int64_t)c * ca + (int64_t)d * cc) >> 14),
                                     (int32_t)(((int64_t)c * cb + (int64_t)d * cd) >> 14),
                                     (int32_t)(((int64_t)a * arg_x + (int64_t)b * arg_y) >> 14) + tx,
                                     (int32_t)(((int64_t)c * arg_x + (int64_t)d * arg_y) >> 14) + ty,
                                     (uint16_t)(depth + 1U), point_count, contour_count)) return 0;
        } while (flags & 0x0020U);
    }
    return 1;
}

static int ui_ttf_points_for_glyph(uint16_t glyph, uint16_t *point_count, uint16_t *contour_count)
{
    *point_count = 0;
    *contour_count = 0;
    return ui_ttf_append_glyph(glyph, 16384, 0, 0, 16384, 0, 0, 0, point_count, contour_count);
}

static int32_t ui_ttf_scale_point(int32_t value, int32_t scale)
{
    return (int32_t)((int64_t)value * scale);
}

static int ui_ttf_add_edge(uint32_t *count, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (*count >= UI_TTF_EDGES_MAX || (x0 == x1 && y0 == y1)) {
        return *count < UI_TTF_EDGES_MAX;
    }
    ui_ttf_edges[*count].x0 = x0;
    ui_ttf_edges[*count].y0 = y0;
    ui_ttf_edges[*count].x1 = x1;
    ui_ttf_edges[*count].y1 = y1;
    ++*count;
    return 1;
}

static int ui_ttf_add_quad(uint32_t *count, struct ui_ttf_point a, struct ui_ttf_point b,
                           struct ui_ttf_point c, int32_t origin_x, int32_t origin_y,
                           int32_t scale, int32_t baseline)
{
    int32_t previous_x = origin_x + ui_ttf_scale_point(a.x, scale);
    int32_t previous_y = origin_y + baseline - ui_ttf_scale_point(a.y, scale);
    for (int32_t step = 1; step <= 4; ++step) {
        int32_t left = 4 - step;
        int32_t px = (left * left * a.x + 2 * left * step * b.x + step * step * c.x) / 16;
        int32_t py = (left * left * a.y + 2 * left * step * b.y + step * step * c.y) / 16;
        int32_t next_x = origin_x + ui_ttf_scale_point(px, scale);
        int32_t next_y = origin_y + baseline - ui_ttf_scale_point(py, scale);
        if (!ui_ttf_add_edge(count, previous_x, previous_y, next_x, next_y)) return 0;
        previous_x = next_x;
        previous_y = next_y;
    }
    return 1;
}

static int ui_ttf_build_edges(uint16_t glyph, uint32_t cell_h, uint32_t x, uint32_t y, uint32_t *edge_count)
{
    uint16_t points, contours;
    int16_t bearing;
    uint16_t advance = ui_ttf_advance(glyph, &bearing);
    int32_t line_height = ui_ttf.ascender - ui_ttf.descender;
    int32_t scale, baseline, origin_x, origin_y;
    uint32_t edges = 0;
    if (!advance || !line_height || !ui_ttf_points_for_glyph(glyph, &points, &contours)) return 0;
    scale = (int32_t)(((uint64_t)cell_h << 16) / line_height);
    if (scale <= 0) return 0;
    origin_x = ((int32_t)x << 16) - ui_ttf_scale_point(bearing, scale);
    origin_y = ((int32_t)y << 16) + (((int32_t)cell_h << 16) - ui_ttf_scale_point(line_height, scale)) / 2;
    baseline = ui_ttf_scale_point(ui_ttf.ascender, scale);
    for (uint16_t contour = 0, start = 0; contour < contours; ++contour) {
        uint16_t end = ui_ttf_contours[contour];
        struct ui_ttf_point first = ui_ttf_points[start];
        struct ui_ttf_point last = ui_ttf_points[end];
        struct ui_ttf_point previous;
        struct ui_ttf_point off_curve;
        struct ui_ttf_point contour_start;
        uint8_t has_off_curve = 0;
        if (first.on_curve) previous = first;
        else if (last.on_curve) previous = last;
        else {
            previous.x = (first.x + last.x) / 2;
            previous.y = (first.y + last.y) / 2;
            previous.on_curve = 1;
        }
        contour_start = previous;
        for (uint16_t index = start; index <= end; ++index) {
            struct ui_ttf_point point = ui_ttf_points[index];
            if (point.on_curve) {
                if (has_off_curve) {
                    if (!ui_ttf_add_quad(&edges, previous, off_curve, point, origin_x, origin_y, scale, baseline)) return 0;
                    has_off_curve = 0;
                } else if (!ui_ttf_add_edge(&edges,
                           origin_x + ui_ttf_scale_point(previous.x, scale), origin_y + baseline - ui_ttf_scale_point(previous.y, scale),
                           origin_x + ui_ttf_scale_point(point.x, scale), origin_y + baseline - ui_ttf_scale_point(point.y, scale))) return 0;
                previous = point;
            } else if (has_off_curve) {
                struct ui_ttf_point middle;
                middle.x = (off_curve.x + point.x) / 2;
                middle.y = (off_curve.y + point.y) / 2;
                middle.on_curve = 1;
                if (!ui_ttf_add_quad(&edges, previous, off_curve, middle, origin_x, origin_y, scale, baseline)) return 0;
                previous = middle;
                off_curve = point;
            } else {
                off_curve = point;
                has_off_curve = 1;
            }
        }
        if (has_off_curve) {
            if (!ui_ttf_add_quad(&edges, previous, off_curve, contour_start,
                                 origin_x, origin_y, scale, baseline)) return 0;
        } else if (!ui_ttf_add_edge(&edges,
                   origin_x + ui_ttf_scale_point(previous.x, scale), origin_y + baseline - ui_ttf_scale_point(previous.y, scale),
                   origin_x + ui_ttf_scale_point(contour_start.x, scale), origin_y + baseline - ui_ttf_scale_point(contour_start.y, scale))) return 0;
        start = (uint16_t)(end + 1U);
    }
    *edge_count = edges;
    return 1;
}

static int ui_ttf_contains(uint32_t edges, int32_t x, int32_t y)
{
    int winding = 0;
    for (uint32_t i = 0; i < edges; ++i) {
        const struct ui_ttf_edge *edge = &ui_ttf_edges[i];
        int64_t cross;
        if ((edge->y0 <= y && edge->y1 <= y) || (edge->y0 > y && edge->y1 > y)) continue;
        cross = (int64_t)(edge->x1 - edge->x0) * (y - edge->y0) -
                (int64_t)(x - edge->x0) * (edge->y1 - edge->y0);
        if (edge->y1 > edge->y0) {
            if (cross > 0) ++winding;
        } else if (cross < 0) {
            --winding;
        }
    }
    return winding != 0;
}

static uint32_t ui_blend_color(uint32_t background, uint32_t foreground, uint8_t alpha)
{
    uint32_t inverse = 255U - alpha;
    uint32_t red = (((foreground >> 16) & 0xffU) * alpha + ((background >> 16) & 0xffU) * inverse + 127U) / 255U;
    uint32_t green = (((foreground >> 8) & 0xffU) * alpha + ((background >> 8) & 0xffU) * inverse + 127U) / 255U;
    uint32_t blue = ((foreground & 0xffU) * alpha + (background & 0xffU) * inverse + 127U) / 255U;
    return (red << 16) | (green << 8) | blue;
}

static uint32_t ui_surface_color(const struct leonos_ui_surface *surface, uint32_t x, uint32_t y)
{
    return (!surface || !surface->pixels || x >= surface->width || y >= surface->height) ? 0 : surface->pixels[(uint64_t)y * surface->stride + x];
}

static void ui_tofu(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t fg, uint32_t bg, int transparent)
{
    if (!transparent) leonos_ui_rect(surface, x, y, w, h, bg);
    if (w < 3 || h < 3) return;
    leonos_ui_rect(surface, x + 1, y + 1, w - 2, 1, fg);
    leonos_ui_rect(surface, x + 1, y + h - 2, w - 2, 1, fg);
    leonos_ui_rect(surface, x + 1, y + 1, 1, h - 2, fg);
    leonos_ui_rect(surface, x + w - 2, y + 1, 1, h - 2, fg);
}

static void ui_ttf_draw(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        uint32_t codepoint, uint32_t fg, uint32_t bg, int transparent)
{
    uint16_t glyph = ui_ttf_glyph(codepoint);
    uint32_t edges;
    if (codepoint == ' ' || codepoint == '\t') {
        if (!transparent) {
            leonos_ui_rect(surface, x, y, w, h, bg);
        }
        return;
    }
    if (!glyph) glyph = ui_ttf_glyph('?');
    if (!glyph || !ui_ttf_build_edges(glyph, h, x, y, &edges)) {
        ui_tofu(surface, x, y, w, h, fg, bg, transparent);
        return;
    }
    for (uint32_t row = 0; row < h; ++row) {
        for (uint32_t col = 0; col < w; ++col) {
            uint8_t alpha;
            if (ui_theme_is_metro()) {
                uint32_t hits = 0;
                for (int32_t sy = 1; sy < 8; sy += 2)
                    for (int32_t sx = 1; sx < 8; sx += 2)
                        hits += ui_ttf_contains(edges, ((int32_t)(x + col) << 16) + sx * 8192,
                                                 ((int32_t)(y + row) << 16) + sy * 8192);
                alpha = (uint8_t)((hits * 255U + 8U) / 16U);
            } else {
                alpha = ui_ttf_contains(edges, ((int32_t)(x + col) << 16) + 32768,
                                         ((int32_t)(y + row) << 16) + 32768) ? 255U : 0;
            }
            if (alpha) {
                uint32_t base = transparent ? ui_surface_color(surface, x + col, y + row) : bg;
                leonos_ui_pixel(surface, x + col, y + row, ui_blend_color(base, fg, alpha));
            } else if (!transparent) {
                leonos_ui_pixel(surface, x + col, y + row, bg);
            }
        }
    }
}

uint32_t ui_strlen(const char *text)
{
    uint32_t length = 0;
    while (text && text[length]) ++length;
    return length;
}

static int ui_utf8_cont(uint8_t byte)
{
    return (byte & 0xc0U) == 0x80U;
}

uint32_t ui_decode_utf8(const char *text, uint32_t len, uint32_t off, uint32_t *byte_len)
{
    const uint8_t *bytes = (const uint8_t *)text;
    uint8_t first;
    if (byte_len) *byte_len = 1;
    if (!text || off >= len) return LEONOS_TEXT_REPLACEMENT_CHAR;
    first = bytes[off];
    if (first < 0x80U) return first;
    if (first < 0xc2U) return LEONOS_TEXT_REPLACEMENT_CHAR;
    if (first < 0xe0U && off + 1U < len && ui_utf8_cont(bytes[off + 1U])) {
        if (byte_len) *byte_len = 2;
        return ((uint32_t)(first & 0x1fU) << 6) | (bytes[off + 1U] & 0x3fU);
    }
    if (first < 0xf0U && off + 2U < len && ui_utf8_cont(bytes[off + 1U]) && ui_utf8_cont(bytes[off + 2U]) &&
        !(first == 0xe0U && bytes[off + 1U] < 0xa0U) && !(first == 0xedU && bytes[off + 1U] >= 0xa0U)) {
        if (byte_len) *byte_len = 3;
        return ((uint32_t)(first & 0x0fU) << 12) | ((uint32_t)(bytes[off + 1U] & 0x3fU) << 6) | (bytes[off + 2U] & 0x3fU);
    }
    if (first < 0xf5U && off + 3U < len && ui_utf8_cont(bytes[off + 1U]) && ui_utf8_cont(bytes[off + 2U]) && ui_utf8_cont(bytes[off + 3U]) &&
        !(first == 0xf0U && bytes[off + 1U] < 0x90U) && !(first == 0xf4U && bytes[off + 1U] >= 0x90U)) {
        if (byte_len) *byte_len = 4;
        return ((uint32_t)(first & 7U) << 18) | ((uint32_t)(bytes[off + 1U] & 0x3fU) << 12) |
               ((uint32_t)(bytes[off + 2U] & 0x3fU) << 6) | (bytes[off + 3U] & 0x3fU);
    }
    return LEONOS_TEXT_REPLACEMENT_CHAR;
}

static int ui_is_wide_codepoint(uint32_t codepoint)
{
    return (codepoint >= 0x1100U && codepoint <= 0x115fU) || codepoint == 0x2329U || codepoint == 0x232aU ||
           (codepoint >= 0x2e80U && codepoint <= 0xa4cfU) || (codepoint >= 0xac00U && codepoint <= 0xd7a3U) ||
           (codepoint >= 0xf900U && codepoint <= 0xfaffU) || (codepoint >= 0x20000U && codepoint <= 0x3fffdU) ||
           (codepoint >= 0xfe10U && codepoint <= 0xfe6fU) || (codepoint >= 0xff00U && codepoint <= 0xffe6U);
}

uint32_t ui_cell_width(uint32_t codepoint)
{
    if (!codepoint || codepoint == '\n' || codepoint == '\r') return 0;
    if (codepoint == '\t') return 4;
    return ui_is_wide_codepoint(codepoint) ? 2U : 1U;
}

int ui_layout_utf8(const char *text, uint32_t byte_len, struct leonos_text_glyph *glyphs, uint32_t capacity, struct leonos_text_layout *out)
{
    uint32_t offset = 0, count = 0, cells = 0, pixels = 0;
    if (!text) {
        if (out) { out->text = text; out->byte_len = 0; out->capacity = capacity; out->count = 0; out->total_cells = 0; out->total_px = 0; out->glyphs = glyphs; }
        return 0;
    }
    if (!byte_len) byte_len = ui_strlen(text);
    if (leonos_text_layout_utf8(text, byte_len, glyphs, capacity, out) == 0) return 0;
    while (offset < byte_len) {
        uint32_t size = 1;
        uint32_t codepoint = ui_decode_utf8(text, byte_len, offset, &size);
        uint32_t width = ui_cell_width(codepoint);
        uint32_t pixel_width = ui_ttf_pixel_width(codepoint, LEONOS_FONT_H,
                                                  width * LEONOS_FONT_W);
        if (count < capacity) { glyphs[count].codepoint = codepoint; glyphs[count].byte_offset = offset; glyphs[count].byte_len = size; glyphs[count].cell_width = width; glyphs[count].pixel_width = pixel_width; }
        cells += width;
        pixels += pixel_width;
        ++count;
        offset += size;
    }
    if (out) { out->text = text; out->byte_len = byte_len; out->capacity = capacity; out->count = count; out->total_cells = cells; out->total_px = pixels; out->glyphs = glyphs; }
    return 0;
}

uint32_t ui_next_codepoint_offset(const char *text, uint32_t len, uint32_t pos)
{
    uint32_t size = 1;
    if (!text || pos >= len) return len;
    (void)ui_decode_utf8(text, len, pos, &size);
    return pos + size > len ? len : pos + size;
}

uint32_t ui_prev_codepoint_offset(const char *text, uint32_t pos)
{
    uint32_t previous = 0, current = 0, len = ui_strlen(text);
    if (!text || !pos) return 0;
    if (pos > len) pos = len;
    while (current < pos) { previous = current; current = ui_next_codepoint_offset(text, len, current); }
    return previous;
}

uint32_t ui_text_cells_between(const char *text, uint32_t start, uint32_t end)
{
    uint32_t cells = 0, len = ui_strlen(text);
    if (!text) return 0;
    if (start > len) start = len;
    if (end > len) end = len;
    while (start < end) { uint32_t size = 1; cells += ui_cell_width(ui_decode_utf8(text, len, start, &size)); start += size; }
    return cells;
}

uint32_t ui_text_pixels_between(const char *text, uint32_t start, uint32_t end)
{
    uint32_t pixels = 0, len = ui_strlen(text);
    if (!text) return 0;
    if (start > len) start = len;
    if (end > len) end = len;
    while (start < end) {
        uint32_t size = 1;
        uint32_t codepoint = ui_decode_utf8(text, len, start, &size);
        uint32_t cell_width = ui_cell_width(codepoint);
        pixels += ui_ttf_pixel_width(codepoint, LEONOS_FONT_H,
                                     cell_width * LEONOS_FONT_W);
        start += size;
    }
    return pixels;
}

uint32_t ui_byte_offset_for_cell(const char *text, uint32_t len, uint32_t start, uint32_t target_cell)
{
    uint32_t cells = 0;
    while (text && start < len) { uint32_t size = 1, width = ui_cell_width(ui_decode_utf8(text, len, start, &size)); if (cells + width > target_cell) break; cells += width; start += size; if (cells >= target_cell) break; }
    return start;
}

uint32_t ui_byte_offset_for_pixel(const char *text, uint32_t len, uint32_t start,
                                  uint32_t target_pixel)
{
    uint32_t pixels = 0;
    while (text && start < len) {
        uint32_t size = 1;
        uint32_t codepoint = ui_decode_utf8(text, len, start, &size);
        uint32_t cell_width = ui_cell_width(codepoint);
        uint32_t width = ui_ttf_pixel_width(codepoint, LEONOS_FONT_H,
                                            cell_width * LEONOS_FONT_W);
        if (pixels + width > target_pixel) break;
        pixels += width;
        start += size;
    }
    return start;
}

uint32_t leonos_ui_text_width(const char *text)
{
    struct leonos_text_layout layout;
    struct leonos_text_glyph glyphs[UI_LAYOUT_GLYPH_MAX];
    ui_layout_utf8(text, 0, glyphs, UI_LAYOUT_GLYPH_MAX, &layout);
    return layout.total_px;
}

uint32_t leonos_ui_text_fit_chars(uint32_t pixel_width)
{
    return pixel_width / LEONOS_FONT_W;
}

void ui_codepoint(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t codepoint, uint32_t cell_width, uint32_t fg, uint32_t bg, int transparent)
{
    uint32_t pixel_width;
    if (!cell_width) return;
    pixel_width = ui_ttf_pixel_width(codepoint, LEONOS_FONT_H,
                                     cell_width * LEONOS_FONT_W);
    ui_ttf_draw(surface, x, y, pixel_width, LEONOS_FONT_H, codepoint, fg, bg, transparent);
}

void ui_char(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, char character, uint32_t fg, uint32_t bg, int transparent)
{
    ui_codepoint(surface, x, y, (uint8_t)character, 1, fg, bg, transparent);
}

static void ui_draw_layout_text(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t w, const char *text, uint32_t fg, uint32_t bg, int transparent, int clipped)
{
    struct leonos_text_layout layout;
    struct leonos_text_glyph glyphs[UI_LAYOUT_GLYPH_MAX];
    uint32_t draw_x = x;
    if (!transparent && clipped) leonos_ui_rect(surface, x, y, w, LEONOS_FONT_H, bg);
    ui_layout_utf8(text ? text : "", 0, glyphs, UI_LAYOUT_GLYPH_MAX, &layout);
    for (uint32_t i = 0; i < layout.count && i < UI_LAYOUT_GLYPH_MAX; ++i) {
        if (clipped && draw_x + glyphs[i].pixel_width > x + w) break;
        ui_codepoint(surface, draw_x, y, glyphs[i].codepoint, glyphs[i].cell_width, fg, bg, transparent || clipped);
        draw_x += glyphs[i].pixel_width;
    }
}

void leonos_ui_text(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg)
{
    ui_draw_layout_text(surface, x, y, 0, text, fg, bg, 0, 0);
}

void leonos_ui_text_clipped(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t w, const char *text, uint32_t fg, uint32_t bg)
{
    ui_draw_layout_text(surface, x, y, w, text, fg, bg, 0, 1);
}

void leonos_ui_text_resized_clipped(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t w, const char *text, uint32_t fg, uint32_t bg, uint32_t cell_w, uint32_t cell_h)
{
    struct leonos_text_layout layout;
    struct leonos_text_glyph glyphs[UI_LAYOUT_GLYPH_MAX];
    uint32_t draw_x = x;
    if (!cell_w) cell_w = LEONOS_FONT_W;
    if (!cell_h) cell_h = LEONOS_FONT_H;
    leonos_ui_rect(surface, x, y, w, cell_h, bg);
    ui_layout_utf8(text ? text : "", 0, glyphs, UI_LAYOUT_GLYPH_MAX, &layout);
    for (uint32_t i = 0; i < layout.count && i < UI_LAYOUT_GLYPH_MAX; ++i) {
        uint32_t glyph_w = (glyphs[i].pixel_width * cell_w + LEONOS_FONT_W / 2U) /
                           LEONOS_FONT_W;
        if (draw_x + glyph_w > x + w) break;
        if (glyphs[i].codepoint != '\t') ui_ttf_draw(surface, draw_x, y, glyph_w, cell_h, glyphs[i].codepoint, fg, bg, 0);
        draw_x += glyph_w;
    }
}

void leonos_ui_text_transparent(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, const char *text, uint32_t fg)
{
    ui_draw_layout_text(surface, x, y, 0, text, fg, 0, 1, 0);
}

void leonos_ui_text_transparent_clipped(struct leonos_ui_surface *surface, uint32_t x, uint32_t y, uint32_t w, const char *text, uint32_t fg)
{
    ui_draw_layout_text(surface, x, y, w, text, fg, 0, 1, 1);
}
