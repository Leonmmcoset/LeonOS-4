// LeonOS osmlayer IPC model: defines endpoints and message-routing records.
// Supports communication between the middle layer, kernel, and user services.

#[derive(Clone, Copy)]
pub struct Endpoint {
    pub pid: u32,
    pub port: u32,
}

static mut WINDOW_SERVER: Option<Endpoint> = None;
/// Clears the window-server endpoint so the registry starts empty at boot.
pub fn init_registry() {
    unsafe {
        WINDOW_SERVER = None;
    }
}
/// Records the window server as (pid, port 1) and returns the stored endpoint.
pub fn register_window_server(pid: u32) -> Result<Endpoint, i32> {
    let endpoint = Endpoint { pid, port: 1 };
    unsafe {
        WINDOW_SERVER = Some(endpoint);
    }
    Ok(endpoint)
}
/// Returns the registered window-server endpoint, if any.
pub fn window_server() -> Option<Endpoint> {
    unsafe { WINDOW_SERVER }
}
