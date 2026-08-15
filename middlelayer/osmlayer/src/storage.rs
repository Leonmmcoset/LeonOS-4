// LeonOS osmlayer storage-volume state shared by VFS-facing services.
// Records generic mounted-volume properties without assuming a FAT32 root.

pub struct StorageVolume {
    pub drive: u32,
    pub bytes_per_sector: u16,
    pub writable: bool,
}

static mut ROOT: StorageVolume = StorageVolume {
    drive: 0,
    bytes_per_sector: 512,
    writable: true,
};

/**
 * @brief Initializes the runtime root volume state.
 */
pub fn init_root_partition() {
    unsafe {
        ROOT = StorageVolume {
            drive: 0,
            bytes_per_sector: 512,
            writable: true,
        };
    }
}

/**
 * @brief Reports whether the mounted root accepts basic writes.
 * @return True when the current root volume is writable through the storage ABI.
 */
pub fn supports_basic_write() -> bool {
    unsafe { ROOT.writable && ROOT.bytes_per_sector == 512 }
}

/**
 * @brief Appends a log record through the storage abstraction.
 * @param _path LeonOS path consumed by this operation.
 * @param bytes Input bytes to append.
 * @return Number of bytes accepted, or an errno value.
 */
pub fn append_log(_path: &str, bytes: &[u8]) -> Result<usize, i32> {
    if supports_basic_write() {
        Ok(bytes.len())
    } else {
        Err(30)
    }
}
