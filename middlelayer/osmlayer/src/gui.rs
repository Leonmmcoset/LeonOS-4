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
/// No-op: the GUI protocol keeps no runtime state to set up.
pub fn init_protocol() {}
/// Reports the GUI client ABI version, currently pinned to 1.
pub fn client_api_version() -> u32 {
    1
}
/// True once a window-server endpoint has been registered with the IPC registry.
pub fn has_window_server() -> bool {
    ipc::window_server().is_some()
}
