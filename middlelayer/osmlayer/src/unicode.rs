// LeonOS osmlayer Unicode service: converts and normalizes text encodings.
// Provides UTF-8/UTF-16 handling and character classification for the VFS.

use core::slice;

const OP_LAYOUT_UTF8: u32 = 1;
const OP_UTF8_TO_UTF16LE: u32 = 2;
const OP_UTF16LE_TO_UTF8: u32 = 3;
const OP_VALIDATE_UTF8: u32 = 4;
const REPLACEMENT: u32 = 0xfffd;

#[repr(C)]
pub struct TextGlyph {
    codepoint: u32,
    byte_offset: u32,
    byte_len: u32,
    cell_width: u32,
    pixel_width: u32,
}

#[repr(C)]
pub struct TextLayout {
    text: *const u8,
    byte_len: u32,
    capacity: u32,
    count: u32,
    total_cells: u32,
    total_px: u32,
    glyphs: *mut TextGlyph,
}

#[repr(C)]
pub struct Utf8ToUtf16 {
    utf8: *const u8,
    utf8_len: u32,
    utf16: *mut u16,
    utf16_capacity: u32,
    utf16_len: u32,
}

#[repr(C)]
pub struct Utf16ToUtf8 {
    utf16: *const u16,
    utf16_len: u32,
    utf8: *mut u8,
    utf8_capacity: u32,
    utf8_len: u32,
}
/// Routes a Unicode op to its converter; null `arg` returns -EINVAL, unknown ops return -ENOSYS.
pub fn dispatch(op: u32, arg: *mut core::ffi::c_void) -> i32 {
    if arg.is_null() {
        return -22;
    }
    match op {
        OP_LAYOUT_UTF8 => layout_utf8(arg.cast::<TextLayout>()),
        OP_UTF8_TO_UTF16LE => utf8_to_utf16(arg.cast::<Utf8ToUtf16>()),
        OP_UTF16LE_TO_UTF8 => utf16_to_utf8(arg.cast::<Utf16ToUtf8>()),
        OP_VALIDATE_UTF8 => validate_utf8(arg.cast::<TextLayout>()),
        _ => -38,
    }
}
/// Lays out UTF-8 text into glyphs, filling cmd.count/total_cells/total_px (and glyphs when capacity allows).
fn layout_utf8(cmd: *mut TextLayout) -> i32 {
    let Some(cmd) = (unsafe { cmd.as_mut() }) else {
        return -22;
    };
    if cmd.text.is_null() || (cmd.capacity != 0 && cmd.glyphs.is_null()) {
        return -22;
    }
    let input = unsafe { slice::from_raw_parts(cmd.text, cmd.byte_len as usize) };
    let glyphs = if cmd.capacity == 0 {
        &mut [][..]
    } else {
        unsafe { slice::from_raw_parts_mut(cmd.glyphs, cmd.capacity as usize) }
    };

    let mut offset = 0usize;
    let mut count = 0usize;
    let mut total_cells = 0u32;
    while offset < input.len() {
        let (codepoint, len, _) = decode_one(input, offset);
        let cells = cell_width(codepoint);
        if count < glyphs.len() {
            glyphs[count] = TextGlyph {
                codepoint,
                byte_offset: offset as u32,
                byte_len: len as u32,
                cell_width: cells,
                pixel_width: cells.saturating_mul(8),
            };
        }
        total_cells = total_cells.saturating_add(cells);
        count += 1;
        offset += len;
    }

    cmd.count = count as u32;
    cmd.total_cells = total_cells;
    cmd.total_px = total_cells.saturating_mul(8);
    0
}
/// Checks every byte run decodes as valid UTF-8; returns -EINVAL at the first bad sequence.
fn validate_utf8(cmd: *mut TextLayout) -> i32 {
    let Some(cmd) = (unsafe { cmd.as_ref() }) else {
        return -22;
    };
    if cmd.text.is_null() {
        return -22;
    }
    let input = unsafe { slice::from_raw_parts(cmd.text, cmd.byte_len as usize) };
    let mut offset = 0usize;
    while offset < input.len() {
        let (_, len, valid) = decode_one(input, offset);
        if !valid {
            return -22;
        }
        offset += len;
    }
    0
}
/// Converts UTF-8 to UTF-16LE into a caller buffer, setting cmd.utf16_len; bad code points become U+FFFD.
fn utf8_to_utf16(cmd: *mut Utf8ToUtf16) -> i32 {
    let Some(cmd) = (unsafe { cmd.as_mut() }) else {
        return -22;
    };
    if cmd.utf8.is_null() || (cmd.utf16_capacity != 0 && cmd.utf16.is_null()) {
        return -22;
    }
    let input = unsafe { slice::from_raw_parts(cmd.utf8, cmd.utf8_len as usize) };
    let output = if cmd.utf16_capacity == 0 {
        &mut [][..]
    } else {
        unsafe { slice::from_raw_parts_mut(cmd.utf16, cmd.utf16_capacity as usize) }
    };
    let mut offset = 0usize;
    let mut out_len = 0usize;
    while offset < input.len() {
        let (mut cp, len, _) = decode_one(input, offset);
        if cp > 0x10ffff {
            cp = REPLACEMENT;
        }
        if cp <= 0xffff {
            if out_len < output.len() {
                output[out_len] = cp as u16;
            }
            out_len += 1;
        } else {
            let value = cp - 0x10000;
            if out_len < output.len() {
                output[out_len] = 0xd800 | ((value >> 10) as u16);
            }
            out_len += 1;
            if out_len < output.len() {
                output[out_len] = 0xdc00 | ((value & 0x3ff) as u16);
            }
            out_len += 1;
        }
        offset += len;
    }
    cmd.utf16_len = out_len as u32;
    0
}
/// Converts UTF-16 (with surrogate pairs) to UTF-8, NUL-terminating when space allows; lone surrogates become U+FFFD.
fn utf16_to_utf8(cmd: *mut Utf16ToUtf8) -> i32 {
    let Some(cmd) = (unsafe { cmd.as_mut() }) else {
        return -22;
    };
    if cmd.utf16.is_null() || (cmd.utf8_capacity != 0 && cmd.utf8.is_null()) {
        return -22;
    }
    let input = unsafe { slice::from_raw_parts(cmd.utf16, cmd.utf16_len as usize) };
    let output = if cmd.utf8_capacity == 0 {
        &mut [][..]
    } else {
        unsafe { slice::from_raw_parts_mut(cmd.utf8, cmd.utf8_capacity as usize) }
    };
    let mut i = 0usize;
    let mut out_len = 0usize;
    while i < input.len() {
        let unit = input[i];
        let cp = if (0xd800..=0xdbff).contains(&unit) {
            if i + 1 < input.len() {
                let low = input[i + 1];
                if (0xdc00..=0xdfff).contains(&low) {
                    i += 1;
                    0x10000 + ((((unit as u32) - 0xd800) << 10) | ((low as u32) - 0xdc00))
                } else {
                    REPLACEMENT
                }
            } else {
                REPLACEMENT
            }
        } else if (0xdc00..=0xdfff).contains(&unit) {
            REPLACEMENT
        } else {
            unit as u32
        };
        out_len += encode_utf8(cp, output, out_len);
        i += 1;
    }
    if out_len < output.len() {
        output[out_len] = 0;
    }
    cmd.utf8_len = out_len as u32;
    0
}
/// Decodes one UTF-8 code point at `offset`, returning (code point, byte length, valid).
fn decode_one(input: &[u8], offset: usize) -> (u32, usize, bool) {
    let b0 = byte_at(input, offset);
    if b0 < 0x80 {
        return (b0 as u32, 1, true);
    }
    if b0 < 0xc2 {
        return (REPLACEMENT, 1, false);
    }
    if b0 < 0xe0 {
        if offset + 1 >= input.len() {
            return (REPLACEMENT, 1, false);
        }
        let b1 = byte_at(input, offset + 1);
        if !is_cont(b1) {
            return (REPLACEMENT, 1, false);
        }
        return ((((b0 & 0x1f) as u32) << 6) | ((b1 & 0x3f) as u32), 2, true);
    }
    if b0 < 0xf0 {
        if offset + 2 >= input.len() {
            return (REPLACEMENT, 1, false);
        }
        let b1 = byte_at(input, offset + 1);
        let b2 = byte_at(input, offset + 2);
        if !is_cont(b1) || !is_cont(b2) ||
            (b0 == 0xe0 && b1 < 0xa0) ||
            (b0 == 0xed && b1 >= 0xa0) {
            return (REPLACEMENT, 1, false);
        }
        return (
            (((b0 & 0x0f) as u32) << 12) |
                (((b1 & 0x3f) as u32) << 6) |
                ((b2 & 0x3f) as u32),
            3,
            true,
        );
    }
    if b0 < 0xf5 {
        if offset + 3 >= input.len() {
            return (REPLACEMENT, 1, false);
        }
        let b1 = byte_at(input, offset + 1);
        let b2 = byte_at(input, offset + 2);
        let b3 = byte_at(input, offset + 3);
        if !is_cont(b1) || !is_cont(b2) || !is_cont(b3) ||
            (b0 == 0xf0 && b1 < 0x90) ||
            (b0 == 0xf4 && b1 >= 0x90) {
            return (REPLACEMENT, 1, false);
        }
        return (
            (((b0 & 0x07) as u32) << 18) |
                (((b1 & 0x3f) as u32) << 12) |
                (((b2 & 0x3f) as u32) << 6) |
                ((b3 & 0x3f) as u32),
            4,
            true,
        );
    }
    (REPLACEMENT, 1, false)
}
/// Reads the byte at `index` without a bounds check; the caller guarantees it is in range.
fn byte_at(input: &[u8], index: usize) -> u8 {
    unsafe { *input.as_ptr().add(index) }
}
/// True when `byte` is a UTF-8 continuation byte (0b10xxxxxx).
fn is_cont(byte: u8) -> bool {
    (byte & 0xc0) == 0x80
}
/// Encodes `cp` as UTF-8 into `output` at `offset`; returns the byte count (1-4).
fn encode_utf8(cp: u32, output: &mut [u8], offset: usize) -> usize {
    let cp = if cp > 0x10ffff { REPLACEMENT } else { cp };
    if cp < 0x80 {
        if offset < output.len() {
            output[offset] = cp as u8;
        }
        1
    } else if cp < 0x800 {
        write_byte(output, offset, 0xc0 | ((cp >> 6) as u8));
        write_byte(output, offset + 1, 0x80 | ((cp & 0x3f) as u8));
        2
    } else if cp < 0x10000 {
        write_byte(output, offset, 0xe0 | ((cp >> 12) as u8));
        write_byte(output, offset + 1, 0x80 | (((cp >> 6) & 0x3f) as u8));
        write_byte(output, offset + 2, 0x80 | ((cp & 0x3f) as u8));
        3
    } else {
        write_byte(output, offset, 0xf0 | ((cp >> 18) as u8));
        write_byte(output, offset + 1, 0x80 | (((cp >> 12) & 0x3f) as u8));
        write_byte(output, offset + 2, 0x80 | (((cp >> 6) & 0x3f) as u8));
        write_byte(output, offset + 3, 0x80 | ((cp & 0x3f) as u8));
        4
    }
}
/// Writes `value` at `offset` only if it fits within the buffer.
fn write_byte(output: &mut [u8], offset: usize, value: u8) {
    if offset < output.len() {
        output[offset] = value;
    }
}
/// Returns the terminal cell width of `cp`: 0 for control chars, 4 for tab, 2 for wide glyphs, else 1.
fn cell_width(cp: u32) -> u32 {
    if cp == 0 || cp == b'\n' as u32 || cp == b'\r' as u32 {
        0
    } else if cp == b'\t' as u32 {
        4
    } else if is_wide(cp) {
        2
    } else {
        1
    }
}
/// True when `cp` falls in a Unicode range rendered with two cells.
fn is_wide(cp: u32) -> bool {
    matches!(
        cp,
        0x1100..=0x115f
            | 0x2329..=0x232a
            | 0x2e80..=0xa4cf
            | 0xac00..=0xd7a3
            | 0xf900..=0xfaff
            | 0xfe10..=0xfe19
            | 0xfe30..=0xfe6f
            | 0xff00..=0xff60
            | 0xffe0..=0xffe6
    )
}
