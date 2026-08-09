#![no_std]
#![allow(dead_code)]

mod fat32;
mod device;
mod gui;
mod ipc;
mod posix;
mod unicode;
mod vfs;

use core::ffi::c_char;
use core::panic::PanicInfo;

#[repr(C)]
pub struct BootModule {
    start: u64,
    end: u64,
    name: *const c_char,
}

#[repr(C)]
pub struct BootInfo {
    magic: u32,
    multiboot_info: u64,
    cmdline: *const c_char,
    bootloader: *const c_char,
    framebuffer_addr: u64,
    framebuffer_width: u32,
    framebuffer_height: u32,
    framebuffer_pitch: u32,
    framebuffer_bpp: u8,
    framebuffer_type: u8,
    framebuffer_red_field_position: u8,
    framebuffer_red_mask_size: u8,
    framebuffer_green_field_position: u8,
    framebuffer_green_mask_size: u8,
    framebuffer_blue_field_position: u8,
    framebuffer_blue_mask_size: u8,
    memory_lower_kib: u64,
    memory_upper_kib: u64,
    mmap_addr: u64,
    mmap_entry_size: u32,
    mmap_entry_count: u32,
    efi_mmap_addr: u64,
    efi_mmap_entry_size: u32,
    efi_mmap_entry_count: u32,
    rsdp_addr: u64,
    efi_system_table: u64,
    modules: [BootModule; 16],
    module_count: u32,
}

#[repr(C)]
pub struct SyscallFrame {
    number: u64,
    args: [u64; 6],
}

#[repr(C)]
pub struct OsmlayerBootSummary {
    abi_version: u32,
    module_count: u32,
    memory_kib: u64,
    root_drive: u32,
}

#[repr(C)]
pub struct LeonosBootModuleInfo {
    start: u64,
    end: u64,
    entry: u64,
    path: *const c_char,
}

#[repr(C)]
pub struct LeonosBootLogState {
    log_x: u32,
    log_y: u32,
    columns: u32,
    rows: u32,
    column: u32,
    row: u32,
    line_count: u32,
}

#[repr(C)]
pub struct LeonosBootHandoff {
    magic: u32,
    version: u32,
    multiboot_magic: u32,
    reserved: u32,
    multiboot_info: u64,
    cmdline: *const c_char,
    bootloader: *const c_char,
    framebuffer_addr: u64,
    framebuffer_width: u32,
    framebuffer_height: u32,
    framebuffer_pitch: u32,
    framebuffer_bpp: u8,
    reserved_fb: [u8; 7],
    mmap_addr: u64,
    mmap_entry_size: u32,
    mmap_entry_count: u32,
    efi_mmap_addr: u64,
    efi_mmap_entry_size: u32,
    efi_mmap_entry_count: u32,
    rsdp_addr: u64,
    efi_system_table: u64,
    loader: LeonosBootModuleInfo,
    kernel: LeonosBootModuleInfo,
    middlelayer: LeonosBootModuleInfo,
    installer_root: LeonosBootModuleInfo,
    middlelayer_api: u64,
    boot_log: LeonosBootLogState,
}

#[repr(C)]
pub struct LeonosKernelServices {
    version: u32,
    reserved: u32,
    log: Option<extern "C" fn(*const c_char)>,
    log_len: Option<extern "C" fn(*const c_char, u64)>,
    read_file: Option<extern "C" fn(*const c_char, *mut core::ffi::c_void, u32, *mut u32) -> i32>,
    write_file: Option<extern "C" fn(*const c_char, *const core::ffi::c_void, u32) -> i32>,
    mkdir: Option<extern "C" fn(*const c_char) -> i32>,
}

#[repr(C)]
pub struct LeonosMiddlelayerApi {
    version: u32,
    reserved: u32,
    init: extern "C" fn(*const BootInfo) -> OsmlayerBootSummary,
    syscall: extern "C" fn(*const SyscallFrame) -> i64,
    selftest: extern "C" fn() -> u32,
    mount_policy: extern "C" fn(*const BootInfo, *mut vfs::LeonosMountPolicy) -> i32,
    unicode_op: extern "C" fn(u32, *mut core::ffi::c_void) -> i32,
    vfs_op: extern "C" fn(u32, *mut core::ffi::c_void) -> i32,
    device_catalog: extern "C" fn(*mut device::LeonosDeviceCatalogQuery) -> i32,
    auth_op: extern "C" fn(u32, *mut core::ffi::c_void) -> i32,
}

const ENOSYS: i64 = 38;
const EINVAL: i64 = 22;
const LEONOS_MIDDLELAYER_API_VERSION: u32 = 5;

static mut SUMMARY: OsmlayerBootSummary = OsmlayerBootSummary {
    abi_version: 1,
    module_count: 0,
    memory_kib: 0,
    root_drive: 0,
};

static mut SERVICES: *const LeonosKernelServices = core::ptr::null();

static API: LeonosMiddlelayerApi = LeonosMiddlelayerApi {
    version: LEONOS_MIDDLELAYER_API_VERSION,
    reserved: 0,
    init: osmlayer_rust_init,
    syscall: osmlayer_rust_syscall,
    selftest: osmlayer_rust_selftest,
    mount_policy: osmlayer_rust_mount_policy,
    unicode_op: osmlayer_rust_unicode_op,
    vfs_op: osmlayer_c_vfs_op,
    device_catalog: osmlayer_c_device_catalog,
    auth_op: osmlayer_c_auth_op,
};

macro_rules! klog {
    ($s:expr) => {{
        unsafe {
            let services = SERVICES.as_ref();
            if let Some(log) = services.and_then(|s| s.log) {
                log(concat!($s, "\0").as_ptr().cast::<c_char>());
            }
        }
    }};
}

unsafe extern "C" {
    safe fn osmlayer_c_vfs_op(op: u32, arg: *mut core::ffi::c_void) -> i32;
    safe fn osmlayer_c_device_catalog(query: *mut device::LeonosDeviceCatalogQuery) -> i32;
    safe fn osmlayer_c_auth_op(op: u32, arg: *mut core::ffi::c_void) -> i32;
    safe fn osmlayer_c_bind_services(services: *const LeonosKernelServices);
    safe fn osmlayer_c_services_selftest() -> i32;
}

#[unsafe(no_mangle)]
pub extern "C" fn osmlayer_module_init(
    services: *const LeonosKernelServices,
    _handoff: *const LeonosBootHandoff,
) -> *const LeonosMiddlelayerApi {
    unsafe {
        SERVICES = services;
    }
    osmlayer_c_bind_services(services);
    klog!("[osmlayer] module ABI bound\n");
    &API
}

#[unsafe(no_mangle)]
pub extern "C" fn osmlayer_rust_init(boot: *const BootInfo) -> OsmlayerBootSummary {
    let boot = unsafe { boot.as_ref() };
    let module_count = boot.map(|b| b.module_count).unwrap_or(0);
    let memory_kib = boot
        .map(|b| b.memory_lower_kib.saturating_add(b.memory_upper_kib))
        .unwrap_or(0);
    vfs::init_root(boot);
    let root_drive = vfs::root_drive();

    unsafe {
        SUMMARY = OsmlayerBootSummary {
            abi_version: 1,
            module_count,
            memory_kib,
            root_drive,
        };
    }

    fat32::init_root_partition();
    ipc::init_registry();
    gui::init_protocol();
    posix::init_linux_abi();

    klog!("[osmlayer] Rust no_std layer initialized\n");

    unsafe {
        OsmlayerBootSummary {
            abi_version: SUMMARY.abi_version,
            module_count: SUMMARY.module_count,
            memory_kib: SUMMARY.memory_kib,
            root_drive: SUMMARY.root_drive,
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn osmlayer_rust_mount_policy(
    boot: *const BootInfo,
    out: *mut vfs::LeonosMountPolicy,
) -> i32 {
    let Some(out) = (unsafe { out.as_mut() }) else {
        return -22;
    };
    let boot = unsafe { boot.as_ref() };
    vfs::init_root(boot);
    *out = vfs::current_policy();
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn osmlayer_rust_syscall(frame: *const SyscallFrame) -> i64 {
    let Some(frame) = (unsafe { frame.as_ref() }) else {
        return -EINVAL;
    };
    posix::dispatch(frame.number, &frame.args)
}

#[unsafe(no_mangle)]
pub extern "C" fn osmlayer_rust_unicode_op(op: u32, arg: *mut core::ffi::c_void) -> i32 {
    unicode::dispatch(op, arg)
}

#[unsafe(no_mangle)]
pub extern "C" fn osmlayer_rust_selftest() -> u32 {
    let mut passed = 0;
    if vfs::resolve_drive_path("0:/system/apps/desktop/desktop.elf").is_some() {
        passed += 1;
    }
    if fat32::supports_basic_write() {
        passed += 1;
    }
    if ipc::register_window_server(1).is_ok() {
        passed += 1;
    }
    if gui::client_api_version() == 1 {
        passed += 1;
    }
    if osmlayer_c_services_selftest() == 1 {
        passed += 1;
    }
    passed
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}
