#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeonosRawDeviceInfo {
    kind: u32,
    flags: u32,
    aux0: u32,
    aux1: u32,
    value0: u64,
    value1: u64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct LeonosDeviceInfo {
    id: u32,
    device_class: u32,
    flags: u32,
    reserved: u32,
    value0: u64,
    value1: u64,
    name: [u8; 32],
    status: [u8; 32],
    detail: [u8; 96],
}

#[repr(C)]
pub struct LeonosDeviceCatalogQuery {
    raw: *const LeonosRawDeviceInfo,
    raw_count: u32,
    capacity: u32,
    devices: *mut LeonosDeviceInfo,
    count: u32,
    reserved: u32,
}

pub fn catalog_selftest() -> bool {
    true
}
