// LeonOS osmlayer IPC model: defines endpoints and message-routing records.
// Supports communication between the middle layer, kernel, and user services.

#[derive(Clone, Copy)]
pub struct Endpoint {
    pub pid: u32,
    pub port: u32,
}

static mut WINDOW_SERVER: Option<Endpoint> = None;
/**
 * @brief Initializes registry.
 */
pub fn init_registry() {
    unsafe {
        WINDOW_SERVER = None;
    }
}
/**
 * @brief Registers window server.
 * @param pid Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
pub fn register_window_server(pid: u32) -> Result<Endpoint, i32> {
    let endpoint = Endpoint { pid, port: 1 };
    unsafe {
        WINDOW_SERVER = Some(endpoint);
    }
    Ok(endpoint)
}
/**
 * @brief Coordinates the window server operation.
 * @return Result, status, or value defined by this API.
 */
pub fn window_server() -> Option<Endpoint> {
    unsafe { WINDOW_SERVER }
}
