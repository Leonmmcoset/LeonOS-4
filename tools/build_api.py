#!/usr/bin/env python3
"""Build .api package files for LeonOS."""
import os
import sys
import struct
import io
import argparse

BLOCK_SIZE = 512
MAX_FILE_SIZE = 32 * 1024 * 1024
API_FORMAT = "leonos-api"
API_FORMAT_VERSION = "1"

def validate_ini_value(label, value, max_len=None):
    if not value:
        raise ValueError(f"{label} is empty")
    if '\0' in value or '\r' in value or '\n' in value:
        raise ValueError(f"{label} contains a control character")
    if max_len is not None and len(value.encode('utf-8')) >= max_len:
        raise ValueError(f"{label} is too long")

def validate_member_name(name):
    if not name:
        raise ValueError("archive member name is empty")
    if name.startswith('/') or '\\' in name or ':' in name:
        raise ValueError(f"unsafe archive member name: {name}")
    parts = name.split('/')
    if any(part in ('', '.', '..') for part in parts):
        raise ValueError(f"unsafe archive member name: {name}")
    if any(ord(ch) < 0x20 for ch in name):
        raise ValueError(f"archive member name contains a control character: {name}")
    if len(name.encode('utf-8')) >= 100:
        raise ValueError(f"archive member name is too long: {name}")

def validate_install_path(path):
    if not path.startswith('0:/programs/'):
        raise ValueError(f"default_path must be under 0:/programs/: {path}")
    parts = path[3:].split('/')
    if any(part in ('', '.', '..') for part in parts):
        raise ValueError(f"unsafe install path: {path}")
    if '\\' in path or ':' in path[2:] or any(ord(ch) < 0x20 for ch in path):
        raise ValueError(f"unsafe install path: {path}")
    if len(path.encode('utf-8')) >= 256:
        raise ValueError(f"install path is too long: {path}")

def octal_str(value, length):
    fmt = f"%0{length - 1}o"
    s = fmt % value
    return s[:length - 1] + '\0'

def checksum(data):
    chk = 0
    for b in data:
        chk += ord(b) if isinstance(b, str) else b
    for i in range(148, 156):
        chk += 32
    return chk

def tar_header(name, size):
    validate_member_name(name)
    if size > MAX_FILE_SIZE:
        raise ValueError(f"member is too large: {name}")
    header = bytearray(BLOCK_SIZE)
    name_bytes = name.encode('utf-8')
    header[0:len(name_bytes)] = name_bytes
    header[100:107] = b'0000644'
    header[107] = 0
    header[108:115] = b'0000000'
    header[115] = 0
    header[116:123] = b'0000000'
    header[123] = 0
    size_str = octal_str(size, 12).encode('utf-8')
    header[124:124+len(size_str)] = size_str
    header[136:147] = b'00000000000'
    header[147] = 0
    header[156] = ord('0')
    header[257:262] = b'ustar'
    header[262] = 0
    header[263] = ord('0')
    header[264] = 0
    chk = checksum(header)
    chk_str = octal_str(chk, 6).encode('utf-8') + b'\0 '
    header[148:148+len(chk_str)] = chk_str
    return bytes(header)

def pad_size(size):
    rem = size % BLOCK_SIZE
    return size + (BLOCK_SIZE - rem if rem else 0)

def build_api_file(name, version, main_exe_arcname, default_path,
                   requires_admin, desktop_shortcut, icon_arcname,
                   files_list, output_path):
    """files_list: list of (local_path, arcname) tuples"""
    validate_ini_value("name", name, 64)
    validate_ini_value("version", version, 16)
    validate_member_name(main_exe_arcname)
    if not main_exe_arcname.endswith('.elf'):
        raise ValueError("main_exe must be a .elf file")
    validate_install_path(default_path)
    if icon_arcname:
        validate_member_name(icon_arcname)
        if not icon_arcname.endswith('.bmp'):
            raise ValueError("icon must be a .bmp file")
    seen = {'install.ini'}
    for local_path, arcname in files_list:
        validate_member_name(arcname)
        if arcname in seen:
            raise ValueError(f"duplicate archive member: {arcname}")
        seen.add(arcname)
        if not os.path.isfile(local_path):
            raise FileNotFoundError(local_path)
        if os.path.getsize(local_path) > MAX_FILE_SIZE:
            raise ValueError(f"member is too large: {local_path}")
    if main_exe_arcname not in seen:
        raise ValueError("main_exe must point to a packaged file")
    if icon_arcname and icon_arcname not in seen:
        raise ValueError("icon must point to a packaged file")

    ini_content = f"""[package]
format={API_FORMAT}
version={API_FORMAT_VERSION}

[app]
name={name}
version={version}
main_exe={main_exe_arcname}
default_path={default_path}
requires_admin={1 if requires_admin else 0}
desktop_shortcut={1 if desktop_shortcut else 0}
icon={icon_arcname}
"""
    ini_data = ini_content.encode('utf-8')
    output_abs = os.path.abspath(output_path)
    output_dir = os.path.dirname(output_abs)
    tmp_output = output_abs + '.tmp'
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    try:
        with open(tmp_output, 'wb') as out:
            hdr = tar_header('install.ini', len(ini_data))
            out.write(hdr)
            out.write(ini_data)
            pad = pad_size(len(ini_data)) - len(ini_data)
            if pad:
                out.write(b'\0' * pad)

            for local_path, arcname in sorted(files_list, key=lambda x: x[1]):
                fsize = os.path.getsize(local_path)
                hdr = tar_header(arcname, fsize)
                out.write(hdr)
                with open(local_path, 'rb') as f:
                    out.write(f.read())
                pad = pad_size(fsize) - fsize
                if pad:
                    out.write(b'\0' * pad)

            out.write(b'\0' * BLOCK_SIZE * 2)
        os.replace(tmp_output, output_abs)
    except Exception:
        if os.path.exists(tmp_output):
            os.unlink(tmp_output)
        raise

    print(f"Created {output_path} ({os.path.getsize(output_abs)} bytes)")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("legacy", nargs="*", help=argparse.SUPPRESS)
    parser.add_argument("--name")
    parser.add_argument("--version")
    parser.add_argument("--main-exe")
    parser.add_argument("--default-path")
    parser.add_argument("--icon", default="")
    parser.add_argument("--requires-admin", action="store_true")
    parser.add_argument("--desktop-shortcut", action="store_true")
    parser.add_argument("--file", dest="files", action="append", nargs=2,
                        metavar=("LOCAL", "ARCHIVE"), default=[])
    parser.add_argument("--output")
    args = parser.parse_args()

    if args.legacy:
        if len(args.legacy) != 2 or any((args.name, args.version,
                                         args.main_exe, args.default_path,
                                         args.output, args.files)):
            parser.error("use either the legacy ELF/output form or named options")
        name = "Hello World"
        version = "1.0.0"
        main_exe = "files/helloworld.elf"
        default_path = "0:/programs/helloworld"
        files = [(args.legacy[0], main_exe)]
        output = args.legacy[1]
        requires_admin = True
        desktop_shortcut = True
        icon = ""
    else:
        required = {
            "--name": args.name,
            "--version": args.version,
            "--main-exe": args.main_exe,
            "--default-path": args.default_path,
            "--output": args.output,
        }
        missing = [label for label, value in required.items() if not value]
        if missing or not args.files:
            parser.error("missing required options: " + ", ".join(missing or ["--file"]))
        name = args.name
        version = args.version
        main_exe = args.main_exe
        default_path = args.default_path
        files = [(local, archive) for local, archive in args.files]
        output = args.output
        requires_admin = args.requires_admin
        desktop_shortcut = args.desktop_shortcut
        icon = args.icon

    build_api_file(
        name=name,
        version=version,
        main_exe_arcname=main_exe,
        default_path=default_path,
        requires_admin=requires_admin,
        desktop_shortcut=desktop_shortcut,
        icon_arcname=icon,
        files_list=files,
        output_path=output,
    )


if __name__ == '__main__':
    main()
