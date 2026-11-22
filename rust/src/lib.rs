use serde_json::{json, Value};
use std::fs::{self, OpenOptions};
use std::io::{self, BufRead, BufReader, Read, Write};
use std::os::fd::AsRawFd;
use std::os::unix::fs::{OpenOptionsExt, PermissionsExt};
use std::os::unix::net::UnixStream;
use std::path::{Path, PathBuf};
use std::time::Duration;

pub mod settings;
pub mod doubao;

pub fn runtime() -> PathBuf {
    let root=std::env::var_os("XDG_RUNTIME_DIR").map(PathBuf::from).unwrap_or_else(||PathBuf::from(format!("/run/user/{}", unsafe {libc::getuid()})));
    root.join("voice-input")
}
pub fn prepare_runtime(path: &Path) -> io::Result<()> {
    unsafe {libc::umask(0o077)};
    fs::create_dir_all(path)?;
    fs::set_permissions(path,fs::Permissions::from_mode(0o700))
}
pub fn lock(path: &Path) -> io::Result<fs::File> {
    let f=OpenOptions::new().write(true).create(true).mode(0o600).open(path)?;
    if unsafe { libc::flock(f.as_raw_fd(), libc::LOCK_EX|libc::LOCK_NB) } != 0 {return Err(io::Error::last_os_error());} Ok(f)
}
pub fn request(path:&Path, request:&Value) -> io::Result<Value> {
    let mut stream=UnixStream::connect(path)?;
    stream.set_read_timeout(Some(Duration::from_secs(3)))?; stream.set_write_timeout(Some(Duration::from_secs(3)))?;
    serde_json::to_writer(&mut stream,request)?; stream.write_all(b"\n")?;
    let mut line=String::new(); BufReader::new(stream).take(8193).read_line(&mut line)?;
    if !line.ends_with('\n') || line.len()>8192 {return Err(io::Error::new(io::ErrorKind::InvalidData,"invalid response"));}
    let result:Value=serde_json::from_str(&line)?;
    if result["ok"] != true {return Err(io::Error::new(io::ErrorKind::Other,result["error"].as_str().unwrap_or("target unavailable").to_string()));} Ok(result)
}
pub fn write_status(runtime:&Path,state:&str,message:&str) -> io::Result<()> {
    let temp=runtime.join(format!("status.{}",std::process::id()));
    let mut f=OpenOptions::new().write(true).create(true).truncate(true).mode(0o600).open(&temp)?;
    serde_json::to_writer(&mut f,&json!({"state":state,"message":message}))?;
    fs::rename(temp,runtime.join("status.json"))?;
    eprintln!("{state}: {message}"); Ok(())
}
// arecord may leave its RIFF length at the requested maximum after SIGINT.
pub fn normalize_wav(input:&[u8]) -> io::Result<Vec<u8>> {
    let bad=||io::Error::new(io::ErrorKind::InvalidData,"invalid recording");
    if input.len()<44 || input.len()>8*1024*1024 || &input[..4]!=b"RIFF" || &input[8..12]!=b"WAVE" {return Err(bad());}
    let mut pos=12usize; let mut format=false; let mut data=None;
    while pos+8<=input.len() {
        let id=&input[pos..pos+4]; let size=u32::from_le_bytes(input[pos+4..pos+8].try_into().unwrap()) as usize; pos+=8;
        if id==b"data" {data=Some(&input[pos..]); break;}
        if size>input.len()-pos {return Err(bad());}
        if id==b"fmt " {
            if size<16 {return Err(bad());}
            format=u16::from_le_bytes(input[pos..pos+2].try_into().unwrap())==1 && u16::from_le_bytes(input[pos+2..pos+4].try_into().unwrap())==1 && u32::from_le_bytes(input[pos+4..pos+8].try_into().unwrap())==16000 && u16::from_le_bytes(input[pos+14..pos+16].try_into().unwrap())==16;
        }
        pos=pos.checked_add(size+(size&1)).ok_or_else(bad)?;
    }
    let data=data.ok_or_else(bad)?; if !format || data.len()<3200 || data.len()%2!=0 {return Err(bad());}
    let n=data.len() as u32; let mut out=Vec::with_capacity(44+data.len());
    out.extend_from_slice(b"RIFF");out.extend_from_slice(&(n+36).to_le_bytes());out.extend_from_slice(b"WAVEfmt ");out.extend_from_slice(&16u32.to_le_bytes());out.extend_from_slice(&1u16.to_le_bytes());out.extend_from_slice(&1u16.to_le_bytes());out.extend_from_slice(&16000u32.to_le_bytes());out.extend_from_slice(&32000u32.to_le_bytes());out.extend_from_slice(&2u16.to_le_bytes());out.extend_from_slice(&16u16.to_le_bytes());out.extend_from_slice(b"data");out.extend_from_slice(&n.to_le_bytes());out.extend_from_slice(data); Ok(out)
}
#[cfg(test)] mod tests { use super::*;
 #[test] fn wav_repairs_stale_length(){let mut x=vec![0u8;3244];x[..4].copy_from_slice(b"RIFF");x[8..16].copy_from_slice(b"WAVEfmt ");x[16..20].copy_from_slice(&16u32.to_le_bytes());x[20..22].copy_from_slice(&1u16.to_le_bytes());x[22..24].copy_from_slice(&1u16.to_le_bytes());x[24..28].copy_from_slice(&16000u32.to_le_bytes());x[34..36].copy_from_slice(&16u16.to_le_bytes());x[36..40].copy_from_slice(b"data");x[40..44].copy_from_slice(&3520000u32.to_le_bytes());let r=normalize_wav(&x).unwrap();assert_eq!(u32::from_le_bytes(r[40..44].try_into().unwrap()),3200);}
 #[test] fn wav_rejects_bad(){assert!(normalize_wav(b"bad").is_err());}
}
