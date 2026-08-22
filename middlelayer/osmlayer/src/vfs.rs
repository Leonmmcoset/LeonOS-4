// LeonOS osmlayer VFS: defines mount, path, and filesystem dispatch logic.
// Applies mount policy and routes filesystem operations to storage backends.

use crate::BootInfo;
use core::ffi::c_char;


pub const MOUNT_POLICY_VERSION: u32 = 2;
pub const MOUNT_MAX_ENTRIES: usize = 8;
pub const MOUNT_PATH_LEN: usize = 64;
pub const MOUNT_SOURCE_LEN: usize = 64;

pub const MOUNT_KIND_NONE: u32 = 0;
pub const MOUNT_KIND_FAT32_BOOT: u32 = 1;
pub const MOUNT_KIND_FAT32_RAMDISK: u32 = 2;
pub const MOUNT_KIND_DEVFS: u32 = 3;
pub const MOUNT_KIND_TARGET_ESP: u32 = 4;
pub const MOUNT_KIND_EXT2_BOOT: u32 = 5;
pub const MOUNT_KIND_TARGET_ROOT: u32 = 6;

pub const MOUNT_FLAG_READONLY: u32 = 0x0000_0001;
pub const MOUNT_FLAG_RUNTIME_ROOT: u32 = 0x0000_0002;
pub const MOUNT_FLAG_DEVICE_TREE: u32 = 0x0000_0004;
pub const MOUNT_FLAG_OPTIONAL: u32 = 0x0000_0008;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum NodeKind {
    Directory,
    File,
    Device,
}

/** Returns whether a path uses the canonical Unix absolute form. */
pub fn path_is_absolute(path: &str) -> bool {
    if !path.starts_with('/') {
        return false;
    }
    let bytes = path.as_bytes();
    let mut index = 0;
    while index < bytes.len() {
        if bytes[index] == b':' || bytes[index] == b'\\' {
            return false;
        }
        index += 1;
    }
    true
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeonosMountEntry {
    pub kind: u32,
    pub flags: u32,
    pub reserved: u32,
    pub module_start: u64,
    pub module_len: u64,
    pub path: [u8; MOUNT_PATH_LEN],
    pub source: [u8; MOUNT_SOURCE_LEN],
}

impl LeonosMountEntry {
    // Returns a zeroed, unmounted entry (kind MOUNT_KIND_NONE).
    pub const fn empty() -> Self {
        Self {
            kind: MOUNT_KIND_NONE,
            flags: 0,
            reserved: 0,
            module_start: 0,
            module_len: 0,
            path: [0; MOUNT_PATH_LEN],
            source: [0; MOUNT_SOURCE_LEN],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeonosMountPolicy {
    pub version: u32,
    pub count: u32,
    pub flags: u32,
    pub reserved: u32,
    pub entries: [LeonosMountEntry; MOUNT_MAX_ENTRIES],
}

impl LeonosMountPolicy {
    // Returns an empty policy at MOUNT_POLICY_VERSION with no mount entries.
    pub const fn empty() -> Self {
        Self {
            version: MOUNT_POLICY_VERSION,
            count: 0,
            flags: 0,
            reserved: 0,
            entries: [LeonosMountEntry::empty(); MOUNT_MAX_ENTRIES],
        }
    }
}

static mut POLICY: LeonosMountPolicy = LeonosMountPolicy::empty();
/// Builds the mount table: installer mode uses ramdisk/ESP/devfs, otherwise ext2 root + FAT32 boot + devfs.
pub fn init_root(boot: Option<&BootInfo>) {
    let mut policy = LeonosMountPolicy::empty();
    let installer = boot
        .map(|b| unsafe { cstr_contains(b.cmdline, b"mode=installer") })
        .unwrap_or(false);

    if installer {
        let mut ramdisk = LeonosMountEntry::empty();
        ramdisk.kind = MOUNT_KIND_FAT32_RAMDISK;
        ramdisk.flags = MOUNT_FLAG_RUNTIME_ROOT | MOUNT_FLAG_READONLY;
        copy_bytes(&mut ramdisk.path, b"/");
        copy_bytes(&mut ramdisk.source, b"leonos-installer-root");
        if let Some((start, len)) = find_module(boot, b"leonos-installer-root") {
            ramdisk.module_start = start;
            ramdisk.module_len = len;
        }
        add_entry(&mut policy, ramdisk);

        let mut devfs = LeonosMountEntry::empty();
        devfs.kind = MOUNT_KIND_DEVFS;
        devfs.flags = MOUNT_FLAG_DEVICE_TREE;
        copy_bytes(&mut devfs.path, b"/dev");
        copy_bytes(&mut devfs.source, b"devfs");
        add_entry(&mut policy, devfs);

        let mut target = LeonosMountEntry::empty();
        target.kind = MOUNT_KIND_TARGET_ROOT;
        target.flags = MOUNT_FLAG_OPTIONAL;
        copy_bytes(&mut target.path, b"/target");
        copy_bytes(&mut target.source, b"installer-target-root");
        add_entry(&mut policy, target);

        let mut target_esp = LeonosMountEntry::empty();
        target_esp.kind = MOUNT_KIND_TARGET_ESP;
        target_esp.flags = MOUNT_FLAG_OPTIONAL;
        copy_bytes(&mut target_esp.path, b"/target/boot");
        copy_bytes(&mut target_esp.source, b"installer-target-esp");
        add_entry(&mut policy, target_esp);
    } else {
        let mut root = LeonosMountEntry::empty();
        root.kind = MOUNT_KIND_EXT2_BOOT;
        root.flags = MOUNT_FLAG_RUNTIME_ROOT;
        copy_bytes(&mut root.path, b"/");
        copy_bytes(&mut root.source, b"ahci-ext2:auto");
        add_entry(&mut policy, root);

        let mut devfs = LeonosMountEntry::empty();
        devfs.kind = MOUNT_KIND_DEVFS;
        devfs.flags = MOUNT_FLAG_DEVICE_TREE;
        copy_bytes(&mut devfs.path, b"/dev");
        copy_bytes(&mut devfs.source, b"devfs");
        add_entry(&mut policy, devfs);

        let mut boot = LeonosMountEntry::empty();
        boot.kind = MOUNT_KIND_FAT32_BOOT;
        boot.flags = 0;
        copy_bytes(&mut boot.path, b"/boot");
        copy_bytes(&mut boot.source, b"ahci-esp:auto");
        add_entry(&mut policy, boot);
    }

    unsafe {
        POLICY = policy;
    }
}
/// Returns a copy of the current mount policy.
pub fn current_policy() -> LeonosMountPolicy {
    unsafe { POLICY }
}
/// Appends `entry` to `policy` if there is room under MOUNT_MAX_ENTRIES.
fn add_entry(policy: &mut LeonosMountPolicy, entry: LeonosMountEntry) {
    let idx = policy.count as usize;
    if idx < MOUNT_MAX_ENTRIES {
        policy.entries[idx] = entry;
        policy.count += 1;
    }
}
/// Copies `src` into fixed `dst`, zero-padding and reserving the last byte for a NUL terminator.
fn copy_bytes(dst: &mut [u8], src: &[u8]) {
    let mut i = 0;
    while i < dst.len() {
        dst[i] = 0;
        i += 1;
    }
    let max = if dst.is_empty() { 0 } else { dst.len() - 1 };
    let mut j = 0;
    while j < src.len() && j < max {
        dst[j] = src[j];
        j += 1;
    }
}
/// Finds the boot module whose NUL-terminated name matches `name`; returns its (start, length).
fn find_module(boot: Option<&BootInfo>, name: &[u8]) -> Option<(u64, u64)> {
    let boot = boot?;
    let count = if boot.module_count < boot.modules.len() as u32 {
        boot.module_count as usize
    } else {
        boot.modules.len()
    };
    let mut i = 0;
    while i < count {
        let module = &boot.modules[i];
        if unsafe { cstr_eq(module.name, name) } && module.end > module.start {
            return Some((module.start, module.end - module.start));
        }
        i += 1;
    }
    None
}
/// True when the NUL-terminated C string at `ptr` equals `needle` exactly.
unsafe fn cstr_eq(ptr: *const c_char, needle: &[u8]) -> bool {
    if ptr.is_null() {
        return needle.is_empty();
    }
    let mut i = 0;
    loop {
        let ch = unsafe { *(ptr as *const u8).add(i) };
        if ch == 0 {
            return i == needle.len();
        }
        if i >= needle.len() || ch != needle[i] {
            return false;
        }
        i += 1;
    }
}
/// True when the C string at `ptr` contains `needle` as a substring.
unsafe fn cstr_contains(ptr: *const c_char, needle: &[u8]) -> bool {
    if ptr.is_null() || needle.is_empty() {
        return false;
    }
    let mut start = 0;
    loop {
        let first = unsafe { *(ptr as *const u8).add(start) };
        if first == 0 {
            return false;
        }
        let mut j = 0;
        loop {
            if j == needle.len() {
                return true;
            }
            let ch = unsafe { *(ptr as *const u8).add(start + j) };
            if ch == 0 || ch != needle[j] {
                break;
            }
            j += 1;
        }
        start += 1;
    }
}
