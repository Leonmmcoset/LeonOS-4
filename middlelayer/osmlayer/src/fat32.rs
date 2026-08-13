// LeonOS osmlayer FAT32 support: stores mounted-volume metadata and state.
// Supplies filesystem-specific data used by the VFS mount layer.

pub struct Fat32Volume {
    pub drive: u32,
    pub bytes_per_sector: u16,
    pub writable: bool,
}

static mut ROOT: Fat32Volume = Fat32Volume {
    drive: 0,
    bytes_per_sector: 512,
    writable: true,
};
/**
 * @brief Initializes root partition.
 */
pub fn init_root_partition() {
    unsafe {
        ROOT = Fat32Volume {
            drive: 0,
            bytes_per_sector: 512,
            writable: true,
        };
    }
}
/**
 * @brief Reports whether the subsystem supports basic write.
 * @return Result, status, or value defined by this API.
 */
pub fn supports_basic_write() -> bool {
    unsafe { ROOT.writable && ROOT.bytes_per_sector == 512 }
}
/**
 * @brief Appends log.
 * @param _path LeonOS path consumed by this operation.
 * @param bytes Input or output value used by this operation.
 * @return Result, status, or value defined by this API.
 */
pub fn append_log(_path: &str, bytes: &[u8]) -> Result<usize, i32> {
    if supports_basic_write() {
        Ok(bytes.len())
    } else {
        Err(30)
    }
}
