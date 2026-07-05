#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import struct
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FONT = "Droid-Sans-Fallback"
DEFAULT_FALLBACK_FONTS = [
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
]
TILE_W = 16
TILE_H = 16
BATCH_TILE_COLS = 32
BATCH_GLYPHS = 512

CJK_SYMBOL_RANGES = (
    (0x2E80, 0x2EFF),  # CJK Radicals Supplement
    (0x2F00, 0x2FDF),  # Kangxi Radicals
    (0x2FF0, 0x2FFF),  # Ideographic Description Characters
    (0x3000, 0x303F),  # CJK Symbols and Punctuation
    (0x31C0, 0x31EF),  # CJK Strokes
    (0x3200, 0x32FF),  # Enclosed CJK Letters and Months
    (0x3300, 0x33FF),  # CJK Compatibility
    (0xFE10, 0xFE1F),  # Vertical Forms
    (0xFE30, 0xFE4F),  # CJK Compatibility Forms
    (0xFF00, 0xFFEF),  # Halfwidth and Fullwidth Forms
)
CJK_UNIFIED_RANGES = (
    (0x4E00, 0x9FFF),  # CJK Unified Ideographs
)
CJK_BMP_RANGES = (
    *CJK_SYMBOL_RANGES,
    (0x3400, 0x4DBF),  # CJK Unified Ideographs Extension A
    *CJK_UNIFIED_RANGES,
    (0xF900, 0xFAFF),  # CJK Compatibility Ideographs
)

DEFAULT_CHARS = (
    "的一是在不了有人和国中大为上个民我以要他时来用们生到作地于出就分对成会可主发年"
    "动同工也能下过子说产种面而方后多定行学法所之进着等部度家电力里如水化高自二理起"
    "小物现实加量都两体制机当使点从业本去把性好应开它合还因由其些然前外天政四日那社"
    "义事平形相全表间样与关各重新线内数正心反你明看原又么利比或但质气第向道命此变条"
    "只没结解问意建月公无系军很情者最立代想已通并提直题党程展五果料象员革位入常文总"
    "次品式活设及管特件长求老头基资边流路级少图山统接知较将组见计别她手角期根论运农"
    "指几九区强放决西被干做必战先回则任取据处队南给色光门即保治北造百规热领七海口东"
    "导器压志世金增争济阶油思术极交受联什认六共权收证改清美再采转更单风切打白教速花"
    "带安场身车例真务具万每目至达走积示议声报斗完类八离华名确才科张信马节话米整空元"
    "况今集温传土许步群广石记需段研界拉林律叫且究观越织装影算低持音众书布复容儿须际"
    "商非验连断深难近矿千周委素技备半办青省列习响约支般史感劳便团往酸历市克何除消构"
    "府称太准精值号率族维划选标写存候毛亲快效斯院查江型眼王按格养易置派层片始却专状"
    "育厂京识适属圆包火住调满县局照参红细引听该铁价严龙飞"
    "你好中文显示测试目录安装设置系统文件窗口打开保存退出帮助确定取消应用保留恢复分辨率缩放"
    "任务栏开始菜单时钟活动窗口桌面记事本管理器终端计算器错误进度完成重启硬盘"
    "打开方式默认程序属性名称类型路径大小项目新建文件夹重命名删除目录非空浏览启动"
    "显示帧缓冲桌面语言英文中文更改保存已还原设为默认扩展名始终使用此应用"
    "扫雷新游戏准备游戏中胜利爆炸剩余地雷系统信息版本内核中间层构建时间版权"
    "运行输入文件路径按回车整数计算器按钮已按下窗口服务测试报告通过失败安全套件"
    "，。！？：；（）【】《》、"
)


def gb2312_level1_chars() -> str:
    chars: list[str] = []
    for high in range(0xB0, 0xD8):
        for low in range(0xA1, 0xFF):
            try:
                chars.append(bytes([high, low]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return "".join(chars)


def codepoints_for_ranges(ranges: tuple[tuple[int, int], ...]) -> set[int]:
    cps: set[int] = set()
    for start, end in ranges:
        cps.update(range(start, end + 1))
    return cps


def coverage_codepoints(name: str) -> set[int]:
    if name == "manual":
        return set()
    if name == "gb2312-level1":
        return {ord(ch) for ch in gb2312_level1_chars()}
    if name == "cjk-unified":
        return codepoints_for_ranges(CJK_SYMBOL_RANGES + CJK_UNIFIED_RANGES)
    if name == "cjk-bmp":
        return codepoints_for_ranges(CJK_BMP_RANGES)
    raise ValueError(f"unknown coverage: {name}")


def tofu_glyph() -> bytes:
    rows = [0] * 16
    rows[1] = 0x7FFE
    rows[14] = 0x7FFE
    for y in range(2, 14):
        rows[y] = 0x4002
    data = bytearray()
    for row in rows:
        data.append((row >> 8) & 0xFF)
        data.append(row & 0xFF)
    return bytes(data)


def render_char_with_font(ch: str, font: str, point_size: int) -> bytes:
    cmd = [
        "convert",
        "-background",
        "black",
        "-fill",
        "white",
        "-font",
        font,
        "-pointsize",
        str(point_size),
        "-size",
        "16x16",
        "-gravity",
        "center",
        f"label:{ch}",
        "-threshold",
        "50%",
        "txt:-",
    ]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0 or "unable to read font" in proc.stderr.lower():
        raise subprocess.CalledProcessError(proc.returncode, cmd, proc.stdout, proc.stderr)
    rows = [0] * 16
    for line in proc.stdout.splitlines():
        if not line or line.startswith("#") or ":" not in line:
            continue
        xy, rest = line.split(":", 1)
        x_s, y_s = xy.split(",", 1)
        x = int(x_s)
        y = int(y_s)
        if 0 <= x < 16 and 0 <= y < 16 and "white" in rest:
            rows[y] |= 0x8000 >> x
    data = bytearray()
    for row in rows:
        data.append((row >> 8) & 0xFF)
        data.append(row & 0xFF)
    return bytes(data)


def render_char(ch: str, fonts: list[str], point_size: int) -> bytes:
    for font in fonts:
        try:
            glyph = render_char_with_font(ch, font, point_size)
            if any(glyph):
                return glyph
        except subprocess.CalledProcessError:
            continue
    return tofu_glyph()


def render_batch_with_font(chars: list[str], font: str, point_size: int) -> list[bytes]:
    if not chars:
        return []
    cols = min(BATCH_TILE_COLS, len(chars))
    rows = math.ceil(len(chars) / cols)
    cmd = [
        "montage",
        "-background",
        "black",
        "-fill",
        "white",
        "-font",
        font,
        "-pointsize",
        str(point_size),
        "-size",
        f"{TILE_W}x{TILE_H}",
        "-gravity",
        "center",
    ]
    cmd.extend(f"caption:{ch}" for ch in chars)
    cmd += [
        "-tile",
        f"{cols}x",
        "-geometry",
        f"{TILE_W}x{TILE_H}+0+0",
        "png:-",
    ]
    montage = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if montage.returncode != 0 or b"unable to read font" in montage.stderr.lower():
        raise subprocess.CalledProcessError(montage.returncode, cmd, montage.stdout,
                                            montage.stderr)
    convert = subprocess.run(
        ["convert", "png:-", "-threshold", "50%", "-depth", "8", "gray:-"],
        input=montage.stdout,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if convert.returncode != 0:
        raise subprocess.CalledProcessError(convert.returncode, convert.args,
                                            convert.stdout, convert.stderr)
    width = cols * TILE_W
    height = rows * TILE_H
    pixels = convert.stdout
    if len(pixels) != width * height:
        raise subprocess.CalledProcessError(1, convert.args, convert.stdout,
                                            f"unexpected raw size {len(pixels)}".encode())
    glyphs: list[bytes] = []
    for idx in range(len(chars)):
        tile_x = (idx % cols) * TILE_W
        tile_y = (idx // cols) * TILE_H
        data = bytearray()
        for y in range(TILE_H):
            bits = 0
            base = (tile_y + y) * width + tile_x
            for x in range(TILE_W):
                if pixels[base + x]:
                    bits |= 0x8000 >> x
            data.append((bits >> 8) & 0xFF)
            data.append(bits & 0xFF)
        glyphs.append(bytes(data))
    return glyphs


def render_chars(cps: list[int], fonts: list[str], point_size: int) -> dict[int, bytes]:
    rendered: dict[int, bytes] = {}
    primary = fonts[0] if fonts else DEFAULT_FONT
    for start in range(0, len(cps), BATCH_GLYPHS):
        batch = cps[start:start + BATCH_GLYPHS]
        chars = [chr(cp) for cp in batch]
        try:
            glyphs = render_batch_with_font(chars, primary, point_size)
        except subprocess.CalledProcessError:
            glyphs = []
        if len(glyphs) != len(batch):
            glyphs = [render_char(ch, fonts, point_size) for ch in chars]
        for cp, ch, glyph in zip(batch, chars, glyphs):
            if not any(glyph) and cp != 0x3000:
                glyph = render_char(ch, fonts, point_size)
            rendered[cp] = glyph
        done = min(start + BATCH_GLYPHS, len(cps))
        print(f"rendered {done}/{len(cps)} glyphs", flush=True)
    return rendered


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate LeonOS 16x16 CJK LBF font")
    parser.add_argument("--out", default="system/fonts/cjk16.lbf")
    parser.add_argument("--font", default=DEFAULT_FONT)
    parser.add_argument("--fallback-font", action="append", default=[])
    parser.add_argument("--point-size", type=int, default=14)
    parser.add_argument("--chars", default=DEFAULT_CHARS)
    parser.add_argument("--coverage", default="cjk-bmp",
                        choices=("manual", "gb2312-level1", "cjk-unified", "cjk-bmp"),
                        help="Unicode range set to merge with --chars")
    parser.add_argument("--no-gb2312-level1", action="store_true",
                        help="Deprecated alias for --coverage manual")
    args = parser.parse_args()

    out = ROOT / args.out
    out.parent.mkdir(parents=True, exist_ok=True)
    coverage = "manual" if args.no_gb2312_level1 else args.coverage
    fonts = [args.font] + args.fallback_font + DEFAULT_FALLBACK_FONTS
    cp_set = {ord(ch) for ch in args.chars if ord(ch) >= 0x80}
    cp_set.update(coverage_codepoints(coverage))
    cps = sorted(cp_set)
    header_size = 24
    index_offset = header_size
    bitmap_offset = index_offset + len(cps) * 8
    bitmaps = bytearray()
    index = bytearray()
    rendered = render_chars(cps, fonts, args.point_size)
    for cp in cps:
        glyph_offset = bitmap_offset + len(bitmaps)
        index += struct.pack("<II", cp, glyph_offset)
        bitmaps += rendered.get(cp, tofu_glyph())
    header = b"LBF1" + struct.pack("<HHHHIII", 16, 16, 32, 0, len(cps), index_offset, bitmap_offset)
    out.write_bytes(header + index + bitmaps)
    print(f"wrote {out} glyphs={len(cps)} bytes={out.stat().st_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
