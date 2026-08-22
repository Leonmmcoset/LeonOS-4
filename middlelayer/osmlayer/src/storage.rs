// LeonOS osmlayer storage-volume state shared by VFS-facing services.
// Records generic mounted-volume properties without assuming a FAT32 root.

pub struct StorageVolume {
    pub volume_id: u32,
    pub bytes_per_sector: u16,
    pub writable: bool,
}

static mut ROOT: StorageVolume = StorageVolume {
    volume_id: 0,
    bytes_per_sector: 512,
    writable: true,
};
/// Resets the root volume to its defaults: id 0, 512-byte sectors, writable.
pub fn init_root_partition() {
    unsafe {
        ROOT = StorageVolume {
            volume_id: 0,
            bytes_per_sector: 512,
            writable: true,
        };
    }
}
/// True when the root volume is writable and uses 512-byte sectors.
pub fn supports_basic_write() -> bool {
    unsafe { ROOT.writable && ROOT.bytes_per_sector == 512 }
}
/// Returns `bytes.len()` if the root accepts writes, otherwise errno 30 (EROFS).
pub fn append_log(_path: &str, bytes: &[u8]) -> Result<usize, i32> {
    if supports_basic_write() {
        Ok(bytes.len())
    } else {
        Err(30)
    }
}
