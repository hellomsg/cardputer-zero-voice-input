//! Local settings operations shared by the configuration app and its CLI.
use serde_json::{json, Value};
use std::fs::{self, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::os::unix::fs::{MetadataExt, OpenOptionsExt, PermissionsExt};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

fn failure(message: &str) -> io::Error { io::Error::new(io::ErrorKind::Other, message.to_owned()) }

pub const PROVIDERS: [&str; 3] = ["deepseek", "openai", "doubao"];

pub fn config_dir() -> io::Result<PathBuf> {
    let home = std::env::var_os("HOME").ok_or_else(|| failure("HOME is unavailable"))?;
    Ok(PathBuf::from(home).join(".config/voice-input"))
}
pub fn provider() -> io::Result<String> {
    let name = match fs::read_to_string(config_dir()?.join("provider")) {
        Ok(s) => s.trim().to_owned(),
        Err(e) if e.kind() == io::ErrorKind::NotFound => "deepseek".to_owned(),
        Err(e) => return Err(e),
    };
    if !PROVIDERS.contains(&name.as_str()) { return Err(failure("Invalid ASR provider")); }
    // Keep the experimental adapter dormant until it has been validated.
    if name == "doubao" { return Ok("deepseek".to_owned()); }
    Ok(name)
}
pub fn select_provider(name: &str) -> io::Result<()> {
    if !["deepseek", "openai"].contains(&name) { return Err(failure("Invalid ASR provider")); }
    save_key(&config_dir()?.join("provider"), name.as_bytes())
}
pub fn provider_key_path(name: &str) -> io::Result<PathBuf> {
    if !PROVIDERS.contains(&name) { return Err(failure("Invalid ASR provider")); }
    Ok(config_dir()?.join(format!("{name}-api-key")))
}
pub fn key_path() -> io::Result<PathBuf> { provider_key_path(&provider()?) }

/// Read at most 1025 bytes and atomically replace the private key file.
/// The caller must not echo or log the input.
pub fn save_key(path: &Path, mut input: impl Read) -> io::Result<()> {
    let mut secret = String::new();
    input.by_ref().take(1025).read_to_string(&mut secret)?;
    let key = secret.trim();
    if key.is_empty() || key.len() > 1024 || !key.bytes().all(|b| b.is_ascii_alphanumeric() || b"-_.".contains(&b)) {
        return Err(failure("Invalid API key"));
    }
    let dir = path.parent().ok_or_else(|| failure("Invalid key path"))?;
    fs::create_dir_all(dir)?;
    let info = fs::symlink_metadata(dir)?;
    if !info.file_type().is_dir() || info.uid() != unsafe { libc::getuid() } { return Err(failure("Unsafe configuration directory")); }
    fs::set_permissions(dir, fs::Permissions::from_mode(0o700))?;
    let mut nonce = [0u8; 16];
    fs::File::open("/dev/urandom")?.read_exact(&mut nonce)?;
    let suffix: String = nonce.iter().map(|v| format!("{v:02x}")).collect();
    let temp = dir.join(format!(".key-{suffix}.tmp"));
    let result = (|| {
        let mut file = OpenOptions::new().write(true).create_new(true).mode(0o600).open(&temp)?;
        file.write_all(key.as_bytes())?;
        file.write_all(b"\n")?;
        file.sync_all()?;
        fs::rename(&temp, path)?;
        Ok(())
    })();
    if result.is_err() { let _ = fs::remove_file(&temp); }
    result
}

fn systemctl(args: &[&str]) -> io::Result<bool> {
    let result = Command::new("systemctl").arg("--user").args(args)
        .stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null()).status()?;
    Ok(result.success())
}

// Read the GNU build ID from an ELF64 little-endian program note without
// executing external utilities or loading the whole application into memory.
fn build_id(path: &Path) -> io::Result<Vec<u8>> {
    let invalid = || failure("Unsupported executable format");
    let mut file = fs::File::open(path)?;
    let mut hdr = [0u8; 64]; file.read_exact(&mut hdr)?;
    if &hdr[..6] != b"\x7fELF\x02\x01" { return Err(invalid()); }
    let u16_at = |x: &[u8]| u16::from_le_bytes(x.try_into().unwrap());
    let u32_at = |x: &[u8]| u32::from_le_bytes(x.try_into().unwrap());
    let u64_at = |x: &[u8]| u64::from_le_bytes(x.try_into().unwrap());
    let phoff=u64_at(&hdr[32..40]); let entsize=u16_at(&hdr[54..56]) as usize; let count=u16_at(&hdr[56..58]) as usize;
    if entsize < 56 || entsize > 1024 || count > 1024 { return Err(invalid()); }
    let length=file.metadata()?.len();
    for i in 0..count {
        let start=phoff.checked_add((i * entsize) as u64).ok_or_else(invalid)?;
        if start.checked_add(56).filter(|end| *end<=length).is_none() { return Err(invalid()); }
        file.seek(SeekFrom::Start(start))?; let mut ph=[0u8;56]; file.read_exact(&mut ph)?;
        if u32_at(&ph[..4])!=4 { continue; }
        let offset=u64_at(&ph[8..16]); let size=u64_at(&ph[32..40]);
        if size>1024*1024 || offset.checked_add(size).filter(|end| *end<=length).is_none() { return Err(invalid()); }
        let mut note=vec![0u8;size as usize];file.seek(SeekFrom::Start(offset))?;file.read_exact(&mut note)?;
        let mut p=0usize;
        while p+12<=note.len() {
            let namesz=u32_at(&note[p..p+4]) as usize;let descsz=u32_at(&note[p+4..p+8]) as usize;let kind=u32_at(&note[p+8..p+12]);p+=12;
            let aligned_name=namesz.checked_add(3).map(|n|n&!3).ok_or_else(invalid)?;
            let aligned_desc=descsz.checked_add(3).map(|n|n&!3).ok_or_else(invalid)?;
            if aligned_name>note.len()-p || aligned_desc>note.len()-p-aligned_name { return Err(invalid()); }
            if kind==3 && namesz==4 && &note[p..p+4]==b"GNU\0" && (1..=64).contains(&descsz) {return Ok(note[p+aligned_name..p+aligned_name+descsz].to_vec());}
            p+=aligned_name+aligned_desc;
        }
    }
    Err(invalid())
}

fn matches_build(path:&str, expected:&[u8])->bool { build_id(Path::new(path)).map(|v|v==expected).unwrap_or(false) }

// Return only a short identifier, and never follow a symlink or read an
// unowned / non-private key file. Very short keys are not exposed.
fn key_hint(path: &Path) -> Option<String> {
    let mut file=OpenOptions::new().read(true).custom_flags(libc::O_NOFOLLOW).open(path).ok()?;
    let meta=file.metadata().ok()?;
    if !meta.file_type().is_file() || meta.uid()!=unsafe{libc::getuid()} || meta.mode()&0o077!=0 || meta.len()>1025 {return None;}
    let mut secret=String::new();Read::by_ref(&mut file).take(1026).read_to_string(&mut secret).ok()?;
    let key=secret.trim();
    if key.len()<8 || key.len()>1024 || !key.bytes().all(|b|b.is_ascii_alphanumeric() || b"-_.".contains(&b)) {return None;}
    Some(format!("{}...{}",&key[..3],&key[key.len()-4..]))
}

pub fn status(path: &Path) -> io::Result<Value> {
    let configured = fs::symlink_metadata(path).map(|m| m.file_type().is_file() && m.uid() == unsafe { libc::getuid() } && m.mode() & 0o077 == 0 && m.len() > 0).unwrap_or(false);
    let (voice, text) = ("voice-input.service", "voice-input-text.service");
    // A package update can leave the running launcher and services using the
    // previous binaries. Clear the notice only after the next system boot.
    let restart_marker = path.parent().ok_or_else(|| failure("Invalid key path"))?.join("restart-required");
    let restart_required = match (fs::read_to_string(&restart_marker), fs::read_to_string("/proc/sys/kernel/random/boot_id")) {
        (Ok(saved), Ok(current)) if saved.trim() == current.trim() && !saved.trim().is_empty() => true,
        (Ok(_), Ok(_)) => { let _ = fs::remove_file(&restart_marker); false },
        (Ok(_), Err(_)) => true,
        _ => false,
    };
    Ok(json!({"provider": provider()?, "key_configured": configured, "key_hint": if configured { key_hint(path) } else { None },
        "restart_required": restart_required,
        "voice_active": systemctl(&["is-active", "--quiet", voice])?,
        "text_active": systemctl(&["is-active", "--quiet", text])?,
        "voice_enabled": systemctl(&["is-enabled", "--quiet", voice])?,
        "text_enabled": systemctl(&["is-enabled", "--quiet", text])?,
        "launcher_compatible": matches_build("/usr/share/APPLaunch/bin/M5CardputerZero-APPLaunch", &[0x32,0x5e,0x7b,0xa6,0x3a,0x58,0x3a,0x9e,0x3f,0xfe,0x5f,0x12,0x61,0xa9,0x16,0xd0,0xc0,0xc7,0xfd,0x2f]),
        "zclaw_compatible": matches_build("/usr/share/APPLaunch/bin/ZClaw", &[0xea,0xde,0xf4,0x81,0x0b,0xc7,0xf6,0xda,0x27,0x6d,0x07,0xb3,0xc5,0x98,0x1c,0xe9,0x43,0xc3,0xee,0x45]),
    }))
}

pub fn set_enabled(enable: bool, path: &Path) -> io::Result<()> {
    if enable && !fs::symlink_metadata(path).map(|m| m.file_type().is_file() && m.uid() == unsafe { libc::getuid() } && m.mode() & 0o077 == 0 && m.len() > 0).unwrap_or(false) {
        return Err(failure("Configure a private API key first"));
    }
    let action = if enable { "enable" } else { "disable" };
    let (voice, text) = ("voice-input.service", "voice-input-text.service");
    if !systemctl(&[action, "--now", text, voice])? {
        return Err(failure("Could not change service state"));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};
    #[test]
    fn key_is_private_atomic_and_invalid_input_preserves_existing_key() {
        let dir=std::env::temp_dir().join(format!("cardputer-settings-{}-{}",std::process::id(),SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos()));
        let path=dir.join("deepseek-api-key");
        save_key(&path, &b"sk-example_1\n"[..]).unwrap();
        assert_eq!(fs::read(&path).unwrap(),b"sk-example_1\n");
        assert_eq!(fs::metadata(&path).unwrap().mode() & 0o777,0o600);
        assert_eq!(fs::metadata(&dir).unwrap().mode() & 0o777,0o700);
        assert_eq!(key_hint(&path).as_deref(),Some("sk-...le_1"));
        fs::set_permissions(&path,fs::Permissions::from_mode(0o644)).unwrap();assert!(key_hint(&path).is_none());
        fs::set_permissions(&path,fs::Permissions::from_mode(0o600)).unwrap();
        assert!(save_key(&path, &b"bad\nheader"[..]).is_err());
        assert_eq!(fs::read(&path).unwrap(),b"sk-example_1\n");
        assert_eq!(fs::read_dir(&dir).unwrap().count(),1);
        fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn key_hint_rejects_short_keys_and_symlinks() {
        use std::os::unix::fs::symlink;
        let dir=std::env::temp_dir().join(format!("cardputer-hint-{}-{}",std::process::id(),SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos()));
        fs::create_dir(&dir).unwrap();let path=dir.join("key");
        fs::write(&path,b"short").unwrap();fs::set_permissions(&path,fs::Permissions::from_mode(0o600)).unwrap();
        assert!(key_hint(&path).is_none());
        fs::write(&path,b"sk-abcdefgh").unwrap();let link=dir.join("link");symlink(&path,&link).unwrap();
        assert!(key_hint(&link).is_none());assert_eq!(key_hint(&path).as_deref(),Some("sk-...efgh"));
        fs::remove_dir_all(dir).unwrap();
    }
    #[test]
    fn parses_and_bounds_checks_elf_build_id() {
        let path=std::env::temp_dir().join(format!("cardputer-elf-{}-{}",std::process::id(),SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos()));
        let mut elf=vec![0u8;140]; elf[..6].copy_from_slice(b"\x7fELF\x02\x01");
        elf[32..40].copy_from_slice(&64u64.to_le_bytes());elf[54..56].copy_from_slice(&56u16.to_le_bytes());elf[56..58].copy_from_slice(&1u16.to_le_bytes());
        elf[64..68].copy_from_slice(&4u32.to_le_bytes());elf[72..80].copy_from_slice(&120u64.to_le_bytes());elf[96..104].copy_from_slice(&20u64.to_le_bytes());
        elf[120..124].copy_from_slice(&4u32.to_le_bytes());elf[124..128].copy_from_slice(&4u32.to_le_bytes());elf[128..132].copy_from_slice(&3u32.to_le_bytes());elf[132..136].copy_from_slice(b"GNU\0");elf[136..140].copy_from_slice(&[1,2,3,4]);
        fs::write(&path,&elf).unwrap();assert_eq!(build_id(&path).unwrap(),[1,2,3,4]);
        elf[124..128].copy_from_slice(&u32::MAX.to_le_bytes());fs::write(&path,&elf).unwrap();assert!(build_id(&path).is_err());
        fs::remove_file(path).unwrap();
    }
}
