// LeonOS osmlayer GUI ABI: defines window and event request structures.
// Marshals GUI operations across the kernel IPC boundary.

use crate::ipc;

#[repr(C)]
pub struct GuiCreateWindow {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
    pub title_ptr: u64,
}

pub fn init_protocol() {}

pub fn client_api_version() -> u32 {
    1
}

pub fn has_window_server() -> bool {
    ipc::window_server().is_some()
}
