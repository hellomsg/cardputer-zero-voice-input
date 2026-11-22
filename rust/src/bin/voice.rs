use cardputer_voice::{lock,normalize_wav,prepare_runtime,request,runtime,write_status};
use serde_json::{json,Value};
use std::fs::{self,File,OpenOptions};
use std::io::{self,Read,Write};
use std::mem::size_of;
use std::os::fd::AsRawFd;
use std::os::unix::fs::{MetadataExt,OpenOptionsExt,PermissionsExt};
use std::path::{Path,PathBuf};
use std::process::{Child,Command,Stdio};
use std::sync::atomic::{AtomicBool,Ordering};
use std::sync::mpsc::{self,Receiver};
use std::time::{Duration,Instant};

const DEVICE:&str="/dev/input/by-path/platform-3f804000.i2c-event";
static STOP:AtomicBool=AtomicBool::new(false);
extern "C" fn stop(_:libc::c_int){STOP.store(true,Ordering::SeqCst);}
fn err(m:&str)->io::Error{io::Error::new(io::ErrorKind::Other,m.to_string())}
fn unique_dir(dir:&Path)->io::Result<PathBuf>{let mut r=[0u8;16];File::open("/dev/urandom")?.read_exact(&mut r)?;let n:String=r.iter().map(|x|format!("{x:02x}")).collect();let p=dir.join(format!("audio-{n}"));fs::create_dir(&p)?;fs::set_permissions(&p,fs::Permissions::from_mode(0o700))?;Ok(p)}
struct Audio {dir:PathBuf}
impl Drop for Audio {fn drop(&mut self){let _=fs::remove_dir_all(&self.dir);}}
fn transcribe(audio:&Path,dir:&Path)->io::Result<String>{
 let provider=cardputer_voice::settings::provider()?;
 let key_path=cardputer_voice::settings::provider_key_path(&provider)?;
 let key=fs::read_to_string(key_path)?;let key=key.trim();
 if key.is_empty()||!key.bytes().all(|b|b.is_ascii_alphanumeric()||b"-_.".contains(&b)){return Err(err("invalid API key"));}
 if fs::metadata(audio)?.len()>8*1024*1024{return Err(err("invalid audio size"));}
 let data=fs::read(audio)?;let wav=normalize_wav(&data)?;let normalized=dir.join("normalized.wav");fs::write(&normalized,wav)?;
 let output=dir.join("response.json");
 if provider == "doubao" { return cardputer_voice::doubao::transcribe(key, &fs::read(&normalized)?); }
 let mut command=Command::new("curl");
 command.args(["--silent","--show-error","--connect-timeout","10","--max-time","10","--config","-","--output"]).arg(&output).args(["--write-out","%{http_code}","--form"]).arg(format!("file=@{};type=audio/wav",normalized.display()));
 if provider == "openai" {command.args(["--form", "model=gpt-transcribe", "https://api.openai.com/v1/audio/transcriptions"]);} else {command.arg("https://api.deepseek.com/v1/tools/asr");}
 let mut child=command.stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null()).spawn()?;
 {let mut stdin=child.stdin.take().ok_or_else(||err("curl stdin unavailable"))?;writeln!(stdin,"header = \"Authorization: Bearer {key}\"")?;}
 let result=child.wait_with_output()?;if !result.status.success(){return Err(err("Network request failed or timed out"));}
 let code=std::str::from_utf8(&result.stdout).unwrap_or("").trim();if code!="200"{return Err(err(&format!("ASR HTTP {code}")));}
 if fs::metadata(&output)?.len()>65536{return Err(err("ASR response too large"));}
 let response:Value=serde_json::from_slice(&fs::read(&output)?)?;
 let text=response["text"].as_str().filter(|s|!s.trim().is_empty()&&s.len()<=4096).ok_or_else(||err("Empty transcription"))?;Ok(text.to_owned())
}
struct Recording {child:Child,audio:Audio,target:String}
fn finish(mut rec:Recording,runtime:PathBuf,interrupted:bool)->io::Result<()> {
 let deadline=Instant::now()+Duration::from_secs(5);
 let status=loop{if let Some(s)=rec.child.try_wait()?{break s;}if Instant::now()>deadline{let _=rec.child.kill();let _=rec.child.wait();return Err(err("Timed out stopping recording"));}std::thread::sleep(Duration::from_millis(50));};
 use std::os::unix::process::ExitStatusExt;
 if !status.success()&&!(interrupted&&(status.code()==Some(1)||status.signal()==Some(libc::SIGINT)||status.code()==Some(130))){return Err(err("Recording failed"));}
 request(&runtime.join("text.sock"),&json!({"op":"check","token":rec.target}))?;
 let text=transcribe(&rec.audio.dir.join("capture.wav"),&rec.audio.dir)?;
 request(&runtime.join("text.sock"),&json!({"op":"commit","token":rec.target,"text":text}))?;Ok(())
}
fn start(dir:&Path)->io::Result<Recording>{
 let value=request(&dir.join("text.sock"),&json!({"op":"capture"}))?;
 let target=value["token"].as_str().ok_or_else(||err("invalid target"))?.to_owned();let audio=Audio{dir:unique_dir(dir)?};
 let child=Command::new("arecord").args(["-q","-D","plughw:1,0","-c","1","-f","S16_LE","-r","16000","-d","15"]).arg(audio.dir.join("capture.wav")).stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null()).spawn()?;
 Ok(Recording{child,audio,target})
}
#[repr(C)]struct InputEvent{time:libc::timeval,kind:u16,code:u16,value:i32}
fn keyboard()->io::Result<File>{
 let f=OpenOptions::new().read(true).custom_flags(libc::O_NONBLOCK).open(DEVICE)?;
 let mut name=[0u8;256];let request=0x80ff4506u64 as libc::c_ulong;
 if unsafe{libc::ioctl(f.as_raw_fd(),request,name.as_mut_ptr())}<0{return Err(io::Error::last_os_error());}
 if name.split(|b|*b==0).next()!=Some(&b"tca8418c"[..]){return Err(err("unexpected keyboard"));}Ok(f)
}
fn cleanup_stale(dir:&Path){if let Ok(entries)=fs::read_dir(dir){for e in entries.flatten(){let p=e.path();let name=e.file_name();let name=name.to_string_lossy();if !name.starts_with("audio-"){continue;}if let Ok(m)=fs::symlink_metadata(&p){if !m.file_type().is_dir()||m.uid()!=unsafe{libc::getuid()}{continue;}if let Ok(files)=fs::read_dir(&p){let safe=files.flatten().all(|e|["capture.wav","normalized.wav","response.json"].contains(&e.file_name().to_string_lossy().as_ref()));if safe{let _=fs::remove_dir_all(p);}}}}}}
fn main()->io::Result<()>{unsafe{libc::signal(libc::SIGTERM,stop as libc::sighandler_t);libc::signal(libc::SIGINT,stop as libc::sighandler_t);}
 let dir=runtime();prepare_runtime(&dir)?;let _lock=lock(&dir.join("recorder.lock"))?;cleanup_stale(&dir);write_status(&dir,"idle","Ready")?;
 let mut keys:Option<File>=None;let mut fn_down=false;let mut shift_left=false;let mut shift_right=false;let mut v_down=false;let mut notice_until:Option<Instant>=None;let mut rec:Option<Recording>=None;let mut worker:Option<Receiver<io::Result<()>>>=None;let mut worker_dir:Option<PathBuf>=None;
 let mut focus_check:Option<Receiver<io::Result<()>>>=None;let mut next_check=Instant::now();
 while !STOP.load(Ordering::SeqCst){
  if notice_until.is_some_and(|deadline|Instant::now()>=deadline){notice_until=None;let _=write_status(&dir,"idle","Ready");}
  if let Some(rx)=&focus_check {if let Ok(result)=rx.try_recv(){focus_check=None;if result.is_err(){if let Some(mut r)=rec.take(){let _=r.child.kill();let _=r.child.wait();drop(r);let _=write_status(&dir,"idle","Input target changed; recording canceled");}}}}
  if rec.is_some() && focus_check.is_none() && Instant::now()>=next_check {let target=rec.as_ref().unwrap().target.clone();let d=dir.clone();let(tx,rx)=mpsc::channel();std::thread::spawn(move||{let result=request(&d.join("text.sock"),&json!({"op":"check","token":target})).map(|_|());let _=tx.send(result);});focus_check=Some(rx);next_check=Instant::now()+Duration::from_secs(1);}
  if let Some(rx)=&worker {if let Ok(result)=rx.try_recv(){worker=None;worker_dir=None;let (state,msg)=match result{Ok(())=>("success","Transcription inserted".to_string()),Err(e)=>("failure",format!("Not inserted: {e}"))};let _=write_status(&dir,state,&msg);notice_until=Some(Instant::now()+Duration::from_secs(3));}}
  if let Some(r)=rec.as_mut(){if let Some(status)=r.child.try_wait()?{focus_check=None;if status.success(){let old=rec.take().unwrap();worker_dir=Some(old.audio.dir.clone());let(tx,rx)=mpsc::channel();let d=dir.clone();let _=write_status(&dir,"transcribing","Transcribing");std::thread::spawn(move||{let _=tx.send(finish(old,d,false));});worker=Some(rx);}else{rec.take();let _=write_status(&dir,"failure","Recording device unavailable");notice_until=Some(Instant::now()+Duration::from_secs(3));}}}
  if keys.is_none(){match keyboard(){Ok(k)=>keys=Some(k),Err(_)=>{std::thread::sleep(Duration::from_secs(3));continue;}}}
  let fd=keys.as_ref().unwrap().as_raw_fd();let mut p=libc::pollfd{fd,events:libc::POLLIN,revents:0};let n=unsafe{libc::poll(&mut p,1,500)};
  if n<0{continue;}if p.revents&(libc::POLLERR|libc::POLLHUP|libc::POLLNVAL)!=0{keys=None;fn_down=false;shift_left=false;shift_right=false;v_down=false;continue;}if n==0{continue;}
  loop{let mut ev=std::mem::MaybeUninit::<InputEvent>::uninit();let r=unsafe{libc::read(fd,ev.as_mut_ptr() as *mut _,size_of::<InputEvent>())};if r<0 {let e=io::Error::last_os_error();if e.kind()!=io::ErrorKind::WouldBlock{keys=None;}break;}if r==0{keys=None;break;}if r as usize!=size_of::<InputEvent>(){break;}let ev=unsafe{ev.assume_init()};if ev.kind==0&&ev.code==3{fn_down=false;shift_left=false;shift_right=false;v_down=false;}if ev.kind!=1{continue;}
   if ev.code==389{fn_down=ev.value!=0;} else if ev.code==42{shift_left=ev.value!=0;} else if ev.code==54{shift_right=ev.value!=0;} else if ev.code==47 {let toggle=ev.value==1&&fn_down&&!v_down;if ev.value!=2{v_down=ev.value==1;}if !toggle{continue;}
    if shift_left||shift_right {
        if rec.is_some()||worker.is_some(){continue;}
        let result=(||->io::Result<(String,bool)>{
            let current=cardputer_voice::settings::provider()?;
            let configured=|name:&str| -> bool {
                cardputer_voice::settings::provider_key_path(name).ok().and_then(|p|fs::symlink_metadata(p).ok()).map(|m|m.file_type().is_file()&&m.uid()==unsafe{libc::getuid()}&&m.len()>0&&m.mode()&0o077==0).unwrap_or(false)
            };
            let other=if current=="deepseek" {"openai"} else {"deepseek"};
            let next=if configured(other) {other} else if configured(&current) {current.as_str()} else {return Ok((String::new(),false))};
            if next!=current {cardputer_voice::settings::select_provider(next)?;}
            Ok((next.to_owned(),true))
        })();
        match result {
          Ok((name,configured))=>{let state=if configured {format!("provider_{}",name)} else {"provider_none".to_owned()};let _=write_status(&dir,&state,if configured {"Current ASR provider"} else {"Configure an ASR API key"});notice_until=Some(Instant::now()+Duration::from_secs(3));},
          Err(_)=>{let _=write_status(&dir,"provider_error","Could not switch ASR provider");notice_until=Some(Instant::now()+Duration::from_secs(3));}
        }
        continue;
    }
    if worker.is_some(){continue;}
    notice_until=None;
    if let Some(mut old)=rec.take(){
        focus_check=None;
        let interrupted=matches!(old.child.try_wait(),Ok(None));
        if interrupted {unsafe{libc::kill(old.child.id() as libc::pid_t,libc::SIGINT);}}
        worker_dir=Some(old.audio.dir.clone()); let(tx,rx)=mpsc::channel(); let d=dir.clone(); let _=write_status(&dir,"transcribing","Transcribing");
        std::thread::spawn(move||{let _=tx.send(finish(old,d,interrupted));}); worker=Some(rx);
    }else{
        match start(&dir){
          Ok(mut recording)=>{ if matches!(recording.child.try_wait(),Ok(None)){let _=write_status(&dir,"recording","Recording; press Fn+V to stop");next_check=Instant::now();rec=Some(recording);}else{let _=recording.child.kill();let _=recording.child.wait();let _=write_status(&dir,"failure","Recording device unavailable");notice_until=Some(Instant::now()+Duration::from_secs(3));}},
          Err(e)=>{let _=write_status(&dir,"failure",&format!("Cannot start: {e}"));notice_until=Some(Instant::now()+Duration::from_secs(3));}
        }
    }
   }
  }
 }
 if let Some(mut r)=rec {let _=r.child.kill();let _=r.child.wait();}if let Some(path)=worker_dir{let _=fs::remove_dir_all(path);}Ok(())
}

#[cfg(test)]
mod tests {
 use super::*;
 use std::os::unix::fs::PermissionsExt;
 #[test]
 fn asr_key_is_sent_on_stdin_not_arguments(){
  let base=std::env::temp_dir().join(format!("voice-rust-test-{}",std::process::id()));fs::create_dir_all(base.join(".config/voice-input")).unwrap();
  fs::write(base.join(".config/voice-input/deepseek-api-key"),"sk-test-private").unwrap();
  fs::write(base.join(".config/voice-input/openai-api-key"),"sk-test-private").unwrap();
  let program=base.join("curl");
  fs::write(&program,"#!/bin/sh\nout=\"\"\nprintf '%s\\n' \"$@\" > \"$HOME/curl-args\"\nwhile [ $# -gt 0 ]; do\n if [ \"$1\" = \"--output\" ]; then shift; out=\"$1\"; fi\n case \"$1\" in *sk-test-private*) exit 42;; esac\n shift\ndone\nread -r header\n[ \"$header\" = 'header = \"Authorization: Bearer sk-test-private\"' ] || exit 43\ncase \"${VOICE_TEST_ASR_MODE:-ok}\" in\n timeout) exit 28;;\n http) printf 429; exit 0;;\n empty) printf '{\"text\":\"  \"}' > \"$out\";;\n invalid) printf invalid > \"$out\";;\n *) printf '{\"text\":\"中文 A1\"}' > \"$out\";;\nesac\nprintf 200\n").unwrap();
  fs::set_permissions(&program,fs::Permissions::from_mode(0o700)).unwrap();
  let old_home=std::env::var_os("HOME");let old_path=std::env::var_os("PATH");std::env::set_var("HOME",&base);std::env::set_var("PATH",&base);
  let mut wav=Vec::new();let n=3200u32;wav.extend_from_slice(b"RIFF");wav.extend_from_slice(&(n+36).to_le_bytes());wav.extend_from_slice(b"WAVEfmt ");wav.extend_from_slice(&16u32.to_le_bytes());wav.extend_from_slice(&1u16.to_le_bytes());wav.extend_from_slice(&1u16.to_le_bytes());wav.extend_from_slice(&16000u32.to_le_bytes());wav.extend_from_slice(&32000u32.to_le_bytes());wav.extend_from_slice(&2u16.to_le_bytes());wav.extend_from_slice(&16u16.to_le_bytes());wav.extend_from_slice(b"data");wav.extend_from_slice(&n.to_le_bytes());wav.resize(44+n as usize,0);
  let input=base.join("sample.wav");fs::write(&input,wav).unwrap();let result=transcribe(&input,&base);
  let deepseek_args=fs::read_to_string(base.join("curl-args")).unwrap();assert!(!deepseek_args.lines().any(|v|v=="languages[]=en"));
  fs::write(base.join(".config/voice-input/provider"),"openai").unwrap();
  let openai=transcribe(&input,&base);
  let openai_args=fs::read_to_string(base.join("curl-args")).unwrap();assert!(openai_args.lines().any(|v|v=="model=gpt-transcribe"));assert!(!openai_args.lines().any(|v|v.starts_with("languages[]=")||v.starts_with("language=")));assert!(openai_args.lines().collect::<Vec<_>>().windows(2).any(|w|w==["--max-time","10"]));
  fs::remove_file(base.join(".config/voice-input/provider")).unwrap();
  std::env::set_var("VOICE_TEST_ASR_MODE","timeout");let timeout=transcribe(&input,&base);
  std::env::set_var("VOICE_TEST_ASR_MODE","http");let http=transcribe(&input,&base);
  std::env::set_var("VOICE_TEST_ASR_MODE","empty");let empty=transcribe(&input,&base);
  std::env::set_var("VOICE_TEST_ASR_MODE","invalid");let invalid=transcribe(&input,&base);
  std::env::remove_var("VOICE_TEST_ASR_MODE");
  if let Some(v)=old_home{std::env::set_var("HOME",v)}else{std::env::remove_var("HOME")};if let Some(v)=old_path{std::env::set_var("PATH",v)}else{std::env::remove_var("PATH")};
  fs::remove_dir_all(base).unwrap();assert_eq!(result.unwrap(),"中文 A1");assert_eq!(openai.unwrap(),"中文 A1");
  assert!(timeout.unwrap_err().to_string().contains("Network request failed or timed out"));
  assert!(http.unwrap_err().to_string().contains("429"));
  assert!(empty.unwrap_err().to_string().contains("Empty transcription"));
  assert!(invalid.is_err());
 }
}
