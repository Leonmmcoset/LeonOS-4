#![no_std]
#![allow(dead_code)]

mod fat32;
mod gui;
mod ipc;
mod posix;
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
    cmdline: *const c_char,
    bootloader: *const c_char,
    framebuffer_addr: u64,
    framebuffer_width: u32,
    framebuffer_height: u32,
    framebuffer_pitch: u32,
    framebuffer_bpp: u8,
    memory_lower_kib: u64,
    memory_upper_kib: u64,
    mmap_addr: u64,
    mmap_entry_size: u32,
    mmap_entry_count: u32,
    rsdp_addr: u64,
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

const ENOSYS: i64 = 38;
const EINVAL: i64 = 22;

static mut SUMMARY: OsmlayerBootSummary = OsmlayerBootSummary {
    abi_version: 1,
    module_count: 0,
    memory_kib: 0,
    root_drive: 0,
};

unsafe extern "C" {
    fn console_printf(fmt: *const c_char, ...);
}

macro_rules! klog {
    ($s:expr) => {{
        unsafe { console_printf(concat!($s, "\0").as_ptr().cast::<c_char>()) }
    }};
}

#[unsafe(no_mangle)]
pub extern "C" fn osmlayer_rust_init(boot: *const BootInfo) -> OsmlayerBootSummary {
    let boot = unsafe { boot.as_ref() };
    let module_count = boot.map(|b| b.module_count).unwrap_or(0);
    let memory_kib = boot
        .map(|b| b.memory_lower_kib.saturating_add(b.memory_upper_kib))
        .unwrap_or(0);

    unsafe {
        SUMMARY = OsmlayerBootSummary {
            abi_version: 1,
            module_count,
            memory_kib,
            root_drive: 0,
        };
    }

    vfs::init_root();
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
pub extern "C" fn osmlayer_rust_syscall(frame: *const SyscallFrame) -> i64 {
    let Some(frame) = (unsafe { frame.as_ref() }) else {
        return -EINVAL;
    };
    posix::dispatch(frame.number, &frame.args)
}

#[unsafe(no_mangle)]
pub extern "C" fn osmlayer_rust_selftest() -> u32 {
    let mut passed = 0;
    if vfs::resolve_drive_path("0:/userland/desktop.elf").is_some() {
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
    passed
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}
