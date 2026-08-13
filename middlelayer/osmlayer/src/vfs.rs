// LeonOS osmlayer VFS: defines mount, path, and filesystem dispatch logic.
// Applies mount policy and routes filesystem operations to storage backends.

use crate::BootInfo;
use core::ffi::c_char;


pub const MOUNT_POLICY_VERSION: u32 = 1;
pub const MOUNT_MAX_ENTRIES: usize = 8;
pub const MOUNT_PATH_LEN: usize = 16;
pub const MOUNT_SOURCE_LEN: usize = 64;

pub const MOUNT_KIND_NONE: u32 = 0;
pub const MOUNT_KIND_FAT32_BOOT: u32 = 1;
pub const MOUNT_KIND_FAT32_RAMDISK: u32 = 2;
pub const MOUNT_KIND_DEVFS: u32 = 3;
pub const MOUNT_KIND_TARGET_ESP: u32 = 4;

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

pub struct ResolvedPath<'a> {
    pub drive: u32,
    pub path: &'a str,
    pub kind: NodeKind,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeonosMountEntry {
    pub drive: u32,
    pub kind: u32,
    pub flags: u32,
    pub reserved: u32,
    pub module_start: u64,
    pub module_len: u64,
    pub path: [u8; MOUNT_PATH_LEN],
    pub source: [u8; MOUNT_SOURCE_LEN],
}

impl LeonosMountEntry {
    /**
 * @brief Coordinates the empty operation.
 * @return Result, status, or value defined by this API.
 */
    pub const fn empty() -> Self {
        Self {
            drive: 0,
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
    pub root_drive: u32,
    pub flags: u32,
    pub entries: [LeonosMountEntry; MOUNT_MAX_ENTRIES],
}

impl LeonosMountPolicy {
    /**
 * @brief Coordinates the empty operation.
 * @return Result, status, or value defined by this API.
 */
    pub const fn empty() -> Self {
        Self {
            version: MOUNT_POLICY_VERSION,
            count: 0,
            root_drive: 0,
            flags: 0,
            entries: [LeonosMountEntry::empty(); MOUNT_MAX_ENTRIES],
        }
    }
}

static mut POLICY: LeonosMountPolicy = LeonosMountPolicy::empty();
/**
 * @brief Initializes root.
 * @param boot Boot information supplied by the loader.
 */
pub fn init_root(boot: Option<&BootInfo>) {
    let mut policy = LeonosMountPolicy::empty();
    let installer = boot
        .map(|b| unsafe { cstr_contains(b.cmdline, b"mode=installer") })
        .unwrap_or(false);

    if installer {
        let mut ramdisk = LeonosMountEntry::empty();
        ramdisk.drive = 0;
        ramdisk.kind = MOUNT_KIND_FAT32_RAMDISK;
        ramdisk.flags = MOUNT_FLAG_RUNTIME_ROOT | MOUNT_FLAG_READONLY;
        copy_bytes(&mut ramdisk.path, b"0:/");
        copy_bytes(&mut ramdisk.source, b"leonos-installer-root");
        if let Some((start, len)) = find_module(boot, b"leonos-installer-root") {
            ramdisk.module_start = start;
            ramdisk.module_len = len;
        }
        add_entry(&mut policy, ramdisk);

        let mut devfs = LeonosMountEntry::empty();
        devfs.drive = 0;
        devfs.kind = MOUNT_KIND_DEVFS;
        devfs.flags = MOUNT_FLAG_DEVICE_TREE;
        copy_bytes(&mut devfs.path, b"0:/dev");
        copy_bytes(&mut devfs.source, b"devfs");
        add_entry(&mut policy, devfs);

        let mut target = LeonosMountEntry::empty();
        target.drive = 1;
        target.kind = MOUNT_KIND_TARGET_ESP;
        target.flags = MOUNT_FLAG_OPTIONAL;
        copy_bytes(&mut target.path, b"1:/");
        copy_bytes(&mut target.source, b"installer-target-esp");
        add_entry(&mut policy, target);
    } else {
        let mut root = LeonosMountEntry::empty();
        root.drive = 0;
        root.kind = MOUNT_KIND_FAT32_BOOT;
        root.flags = MOUNT_FLAG_RUNTIME_ROOT;
        copy_bytes(&mut root.path, b"0:/");
        copy_bytes(&mut root.source, b"ahci-esp:auto");
        add_entry(&mut policy, root);

        let mut devfs = LeonosMountEntry::empty();
        devfs.drive = 0;
        devfs.kind = MOUNT_KIND_DEVFS;
        devfs.flags = MOUNT_FLAG_DEVICE_TREE;
        copy_bytes(&mut devfs.path, b"0:/dev");
        copy_bytes(&mut devfs.source, b"devfs");
        add_entry(&mut policy, devfs);
    }

    unsafe {
        POLICY = policy;
    }
}
/**
 * @brief Coordinates the current policy operation.
 * @return Result, status, or value defined by this API.
 */
pub fn current_policy() -> LeonosMountPolicy {
    unsafe { POLICY }
}
/**
 * @brief Coordinates the root drive operation.
 * @return Result, status, or value defined by this API.
 */
pub fn root_drive() -> u32 {
    unsafe { POLICY.root_drive }
}
/**
 * @brief Coordinates the resolve drive path operation.
 * @param path LeonOS path consumed by this operation.
 * @return Result, status, or value defined by this API.
 */
pub fn resolve_drive_path(path: &str) -> Option<ResolvedPath<'_>> {
    let bytes = path.as_bytes();
    if bytes.len() < 3 || !bytes[0].is_ascii_digit() || bytes[1] != b':' || bytes[2] != b'/' {
        return None;
    }

    let drive = (bytes[0] - b'0') as u32;
    if !drive_mounted(drive) {
        return None;
    }

    let kind = if path == "0:/dev" {
        NodeKind::Directory
    } else if path.starts_with("0:/dev/") && devfs_mounted(0) {
        NodeKind::Device
    } else if path.ends_with('/') {
        NodeKind::Directory
    } else {
        NodeKind::File
    };

    Some(ResolvedPath {
        drive,
        path: &path[2..],
        kind,
    })
}
/**
 * @brief Coordinates the add entry operation.
 * @param policy Input or output value used by this operation.
 * @param entry Input or output value used by this operation.
 */
fn add_entry(policy: &mut LeonosMountPolicy, entry: LeonosMountEntry) {
    let idx = policy.count as usize;
    if idx < MOUNT_MAX_ENTRIES {
        if (entry.flags & MOUNT_FLAG_RUNTIME_ROOT) != 0 {
            policy.root_drive = entry.drive;
        }
        policy.entries[idx] = entry;
        policy.count += 1;
    }
}
/**
 * @brief Copies bytes.
 * @param dst Input or output value used by this operation.
 * @param src Input or output value used by this operation.
 */
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
/**
 * @brief Coordinates the drive mounted operation.
 * @param drive Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
fn drive_mounted(drive: u32) -> bool {
    let policy = current_policy();
    let mut i = 0;
    while i < policy.count as usize && i < MOUNT_MAX_ENTRIES {
        let entry = policy.entries[i];
        if entry.drive == drive && entry.kind != MOUNT_KIND_NONE {
            return true;
        }
        i += 1;
    }
    false
}
/**
 * @brief Coordinates the devfs mounted operation.
 * @param drive Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
fn devfs_mounted(drive: u32) -> bool {
    let policy = current_policy();
    let mut i = 0;
    while i < policy.count as usize && i < MOUNT_MAX_ENTRIES {
        let entry = policy.entries[i];
        if entry.drive == drive && entry.kind == MOUNT_KIND_DEVFS {
            return true;
        }
        i += 1;
    }
    false
}
/**
 * @brief Finds module.
 * @param boot Boot information supplied by the loader.
 * @param name Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
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
/**
 * @brief Coordinates the cstr eq operation.
 * @param ptr Input or output value used by this operation.
 * @param needle Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
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
/**
 * @brief Coordinates the cstr contains operation.
 * @param ptr Input or output value used by this operation.
 * @param needle Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
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
