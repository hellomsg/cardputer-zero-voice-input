use cardputer_voice::{lock,prepare_runtime,runtime};
use serde_json::{json,Value};
use std::collections::HashMap;
use std::fs::{self,File};
use std::io::{self,BufRead,BufReader,Read,Write};
use std::mem::{size_of,zeroed};
use std::os::fd::{AsRawFd,FromRawFd};
use std::os::unix::ffi::OsStrExt;
use std::os::unix::fs::{FileTypeExt,MetadataExt,PermissionsExt};
use std::os::unix::net::{UnixListener,UnixStream};
use std::path::{Path,PathBuf};
use std::sync::{Arc,Mutex};
use std::time::{Duration,Instant};

#[derive(Clone)]
struct Target {expires:Instant,path:PathBuf,token:Vec<u8>}
struct Broker {dir:PathBuf,tokens:Mutex<HashMap<String,Target>>}
fn fail(msg:&str)->io::Error{io::Error::new(io::ErrorKind::Other,msg.to_string())}
fn read_line(stream: &UnixStream)->io::Result<Value>{let mut line=String::new();BufReader::new(stream).take(8193).read_line(&mut line)?;if !line.ends_with('\n')||line.len()>8192{return Err(fail("invalid request"));} Ok(serde_json::from_str(&line)?)}
fn peer(fd:i32)->io::Result<()> {
    let mut cred:libc::ucred=unsafe{zeroed()};let mut len=size_of::<libc::ucred>() as libc::socklen_t;
    if unsafe{libc::getsockopt(fd,libc::SOL_SOCKET,libc::SO_PEERCRED,&mut cred as *mut _ as *mut _,&mut len)}!=0{return Err(io::Error::last_os_error());}
    if cred.uid!=unsafe{libc::getuid()}{return Err(fail("permission denied"));} Ok(())
}
fn native(path:&Path, msg:&[u8])->io::Result<Vec<u8>> {
    let meta=fs::symlink_metadata(path)?;if !meta.file_type().is_socket()||meta.uid()!=unsafe{libc::getuid()}{return Err(fail("invalid adapter"));}
    let bytes=path.as_os_str().as_bytes(); let mut addr:libc::sockaddr_un=unsafe{zeroed()};
    if bytes.len()>=addr.sun_path.len(){return Err(fail("socket path too long"));}
    addr.sun_family=libc::AF_UNIX as _;for(i,c)in bytes.iter().enumerate(){addr.sun_path[i]=*c as _;}
    let raw=unsafe{libc::socket(libc::AF_UNIX,libc::SOCK_SEQPACKET|libc::SOCK_CLOEXEC,0)};if raw<0{return Err(io::Error::last_os_error());}
    let file=unsafe{File::from_raw_fd(raw)};
    let tv=libc::timeval{tv_sec:1,tv_usec:500000};unsafe{libc::setsockopt(raw,libc::SOL_SOCKET,libc::SO_RCVTIMEO,&tv as *const _ as *const _,size_of::<libc::timeval>() as _);libc::setsockopt(raw,libc::SOL_SOCKET,libc::SO_SNDTIMEO,&tv as *const _ as *const _,size_of::<libc::timeval>() as _);}
    if unsafe{libc::connect(raw,&addr as *const _ as *const _,size_of::<libc::sockaddr_un>() as _)}!=0{return Err(io::Error::last_os_error());} peer(raw)?;
    if unsafe{libc::send(raw,msg.as_ptr() as *const _,msg.len(),libc::MSG_NOSIGNAL)} != msg.len() as isize{return Err(fail("adapter write failed"));}
    let mut buf=[0u8;256];let count=unsafe{libc::recv(raw,buf.as_mut_ptr() as *mut _,buf.len(),0)}; drop(file);
    if count<1 || buf[0]!=b'O'{return Err(fail("target unavailable"));} Ok(buf[1..count as usize].to_vec())
}
fn stream_adapter(path:&Path, msg:&Value)->io::Result<Value>{
    let meta=fs::symlink_metadata(path)?;if !meta.file_type().is_socket()||meta.uid()!=unsafe{libc::getuid()}{return Err(fail("invalid adapter"));}
    let mut stream=UnixStream::connect(path)?;stream.set_read_timeout(Some(Duration::from_millis(500)))?;stream.set_write_timeout(Some(Duration::from_millis(500)))?;peer(stream.as_raw_fd())?;
    serde_json::to_writer(&mut stream,msg)?;stream.write_all(b"\n")?;let result=read_line(&stream)?;
    if result["ok"]!=true{return Err(fail("target unavailable"));}Ok(result)
}
fn capture_adapter(path:&Path)->io::Result<Vec<u8>>{
    if path.extension().and_then(|s|s.to_str())==Some("native") {let v=native(path,b"C")?;if ![40,48,64].contains(&v.len()){return Err(fail("invalid adapter response"));}Ok(v)}
    else {let v=stream_adapter(path,&json!({"op":"capture"}))?;let t=v["token"].as_str().filter(|s|!s.is_empty()&&s.len()<=512).ok_or_else(||fail("invalid adapter response"))?;Ok(t.as_bytes().to_vec())}
}
fn commit_adapter(target:&Target,text:&str)->io::Result<()>{
    if target.path.extension().and_then(|s|s.to_str())==Some("native") {let mut msg=Vec::with_capacity(1+target.token.len()+text.len());msg.push(b'I');msg.extend_from_slice(&target.token);msg.extend_from_slice(text.as_bytes());if !native(&target.path,&msg)?.is_empty(){return Err(fail("invalid adapter response"));}}
    else {let token=std::str::from_utf8(&target.token).map_err(|_|fail("invalid token"))?;stream_adapter(&target.path,&json!({"op":"commit","token":token,"text":text}))?;}Ok(())
}
fn random_token()->io::Result<String>{let mut x=[0u8;32];File::open("/dev/urandom")?.read_exact(&mut x)?;Ok(x.iter().map(|b|format!("{b:02x}")).collect())}
impl Broker {
 fn dispatch(&self,req:Value)->io::Result<Value>{match req["op"].as_str(){
 Some("capture")=>{let mut choices=Vec::new();for entry in fs::read_dir(&self.dir)?{let p=entry?.path();let name=p.file_name().and_then(|v|v.to_str()).unwrap_or("");if !name.starts_with("adapter-")||!(name.ends_with(".native")||name.ends_with(".sock")){continue;}if let Ok(token)=capture_adapter(&p){choices.push((p,token));}}
 if choices.len()!=1{return Err(fail("No unique input target"));}let token=random_token()?;let now=Instant::now();let mut entries=self.tokens.lock().unwrap();entries.retain(|_,v|v.expires>now);if entries.len()>=64{return Err(fail("Too many requests"));}let(path,value)=choices.pop().unwrap();entries.insert(token.clone(),Target{expires:now+Duration::from_secs(240),path,token:value});Ok(json!({"ok":true,"token":token}))},
 Some("check")=>{let token=req["token"].as_str().ok_or_else(||fail("invalid request"))?;
 let target=self.tokens.lock().unwrap().get(token).cloned().ok_or_else(||fail("Input target expired"))?;
 if target.expires<=Instant::now() || target.path.extension().and_then(|s|s.to_str())!=Some("native") || capture_adapter(&target.path).ok().as_ref()!=Some(&target.token) {return Err(fail("Input target changed or unavailable"));} Ok(json!({"ok":true}))},
 Some("commit")=>{let token=req["token"].as_str().ok_or_else(||fail("invalid request"))?;let text=req["text"].as_str().filter(|s|!s.is_empty()&&s.len()<=4096).ok_or_else(||fail("invalid request"))?;
 let target=self.tokens.lock().unwrap().remove(token).ok_or_else(||fail("Input target expired"))?;if target.expires<=Instant::now(){return Err(fail("Input target expired"));}commit_adapter(&target,text).map_err(|_|fail("Input target changed or unavailable"))?;Ok(json!({"ok":true}))},
 _=>Err(fail("invalid request"))}}
}
fn handle(mut s:UnixStream,b:Arc<Broker>){let result=(||{s.set_read_timeout(Some(Duration::from_secs(3)))?;s.set_write_timeout(Some(Duration::from_secs(3)))?;peer(s.as_raw_fd())?;b.dispatch(read_line(&s)?)})();let response=result.unwrap_or_else(|e|json!({"ok":false,"error":e.to_string()}));let _=serde_json::to_writer(&mut s,&response);let _=s.write_all(b"\n");}
fn main()->io::Result<()>{let dir=runtime();prepare_runtime(&dir)?;let _lock=lock(&dir.join("broker.lock"))?;let path=dir.join("text.sock");match fs::remove_file(&path){Ok(())=>(),Err(e)if e.kind()==io::ErrorKind::NotFound=>(),Err(e)=>return Err(e)};let listener=UnixListener::bind(&path)?;fs::set_permissions(&path,fs::Permissions::from_mode(0o600))?;let broker=Arc::new(Broker{dir,tokens:Mutex::new(HashMap::new())});for s in listener.incoming(){match s{Ok(s)=>{let b=broker.clone();std::thread::spawn(move||handle(s,b));},Err(e)=>eprintln!("text service: {e}")}}Ok(())}
