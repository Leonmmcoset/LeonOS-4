#[derive(Clone, Copy, PartialEq, Eq)]
pub enum NodeKind {
    Directory,
    File,
    Device,
}

pub struct ResolvedPath<'a> {
    pub drive: u32,
    pub path: &'a str,
    pub kind: NodeKind,
}

pub fn init_root() {}

pub fn resolve_drive_path(path: &str) -> Option<ResolvedPath<'_>> {
    let bytes = path.as_bytes();
    if bytes.len() < 3 || !bytes[0].is_ascii_digit() || bytes[1] != b':' || bytes[2] != b'/' {
        return None;
    }

    let drive = (bytes[0] - b'0') as u32;
    let kind = if path.ends_with('/') {
        NodeKind::Directory
    } else if path.starts_with("0:/dev/") {
        NodeKind::Device
    } else {
        NodeKind::File
    };

    Some(ResolvedPath {
        drive,
        path: &path[2..],
        kind,
    })
}
