//! Doubao speech recognition v3, based on the provider's SAUC protocol.
use flate2::{write::GzEncoder, read::GzDecoder, Compression};
use serde_json::{json,Value};
use std::{fs::File,io::{self,Read,Write}, time::{Duration,Instant}};
use tungstenite::{client::IntoClientRequest, Message, stream::MaybeTlsStream};

fn error(s: &str)->io::Error { io::Error::new(io::ErrorKind::Other,s.to_owned()) }
fn frame(kind:u8, seq:i32, body:&[u8])->io::Result<Vec<u8>> {
    let mut gz=GzEncoder::new(Vec::new(),Compression::default());gz.write_all(body)?;let data=gz.finish()?;
    let mut out=vec![0x11,kind,0x11,0];out.extend(seq.to_be_bytes());out.extend((data.len() as u32).to_be_bytes());out.extend(data);Ok(out)
}
fn response(bytes:&[u8])->io::Result<(bool,Option<String>)> {
    if bytes.len()<4 || bytes[0]>>4!=1 { return Err(error("Invalid Doubao response")); }
    let flags=bytes[1]&15; let kind=bytes[1]>>4;let mut pos=(bytes[0] as usize&15)*4;
    if pos<4 || pos>bytes.len() {return Err(error("Invalid Doubao header"));}
    if flags&1!=0 {pos+=4;} if flags&4!=0 {pos+=4;}
    if kind==15 {return Err(error("Doubao rejected recognition request"));}
    if kind!=9 || pos+4>bytes.len() {return Err(error("Invalid Doubao message"));}
    let size=u32::from_be_bytes(bytes[pos..pos+4].try_into().unwrap()) as usize;pos+=4;
    if size>1024*1024 || pos.checked_add(size)!=Some(bytes.len()) {return Err(error("Invalid Doubao payload"));}
    let mut decoded=Vec::new();
    if bytes[2]&15==1 {GzDecoder::new(&bytes[pos..]).take(1024*1024+1).read_to_end(&mut decoded)?;} else if bytes[2]&15==0 {decoded.extend_from_slice(&bytes[pos..]);} else {return Err(error("Unknown Doubao compression"));}
    if decoded.len()>1024*1024 {return Err(error("Doubao response too large"));}
    let value:Value=serde_json::from_slice(&decoded)?;
    let text=value.pointer("/result/text").and_then(Value::as_str).filter(|s|!s.trim().is_empty()&&s.len()<=4096).map(str::to_owned);
    Ok((flags&2!=0,text))
}

pub fn transcribe(key:&str, wav:&[u8])->io::Result<String> {
    transcribe_at(key,wav,"wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_nostream")
}
fn transcribe_at(key:&str, wav:&[u8], endpoint:&str)->io::Result<String> {
    let mut random=[0u8;16];File::open("/dev/urandom")?.read_exact(&mut random)?;
    let id=format!("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-4{:x}{:02x}-a{:x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",random[0],random[1],random[2],random[3],random[4],random[5],random[6]&15,random[7],random[8]&15,random[9],random[10],random[11],random[12],random[13],random[14],random[15]);
    let mut req=endpoint.into_client_request().map_err(|_|error("Invalid Doubao endpoint"))?;
    req.headers_mut().insert("X-Api-Key",key.parse().map_err(|_|error("Invalid API key"))?);
    req.headers_mut().insert("X-Api-Resource-Id","volc.seedasr.sauc.duration".parse().unwrap());
    req.headers_mut().insert("X-Api-Request-Id",id.parse().unwrap());
    let (mut ws,_)=tungstenite::connect(req).map_err(|_|error("Doubao connection failed"))?;
    let stream=match ws.get_mut(){MaybeTlsStream::Plain(s)=>s,MaybeTlsStream::Rustls(s)=>&mut s.sock,_=>return Err(error("Unsupported TLS"))};
    stream.set_read_timeout(Some(Duration::from_secs(30)))?;stream.set_write_timeout(Some(Duration::from_secs(30)))?;
    let body=json!({"user":{"uid":"voice-input"},"audio":{"format":"wav","codec":"raw","rate":16000,"bits":16,"channel":1},"request":{"model_name":"bigmodel","enable_itn":true,"enable_punc":true}}).to_string();
    ws.send(Message::Binary(frame(0x11,1,body.as_bytes())?)).map_err(|_|error("Doubao request failed"))?;
    let initial=ws.read().map_err(|_|error("Doubao response failed"))?;
    if let Message::Binary(b)=initial {response(&b)?;}else{return Err(error("Invalid Doubao response"));}
    // Send at the documented 200 ms audio rate and drain intermediate responses.
    let chunks:Vec<_>=wav.chunks(6400).collect();
    let mut text=None;
    for (i,chunk) in chunks.iter().enumerate() {
        let last=i+1==chunks.len();let seq=(i as i32)+2;
        let started=Instant::now();
        ws.send(Message::Binary(frame(if last {0x23}else{0x21},if last {-seq}else{seq},chunk)?)).map_err(|_|error("Doubao audio upload failed"))?;
        if !last {
            let stream=match ws.get_mut(){MaybeTlsStream::Plain(s)=>s,MaybeTlsStream::Rustls(s)=>&mut s.sock,_=>return Err(error("Unsupported TLS"))};
            stream.set_read_timeout(Some(Duration::from_millis(180)))?;
            match ws.read() {
                Ok(Message::Binary(b))=>{let (final_frame,new)=response(&b)?;if new.is_some(){text=new;}if final_frame{return text.ok_or_else(||error("Empty recognition result"));}},
                Ok(Message::Close(_))=>return Err(error("Doubao closed connection early")),
                Ok(_)=>{},
                Err(tungstenite::Error::Io(e)) if matches!(e.kind(),io::ErrorKind::WouldBlock|io::ErrorKind::TimedOut)=>{},
                Err(_)=>return Err(error("Doubao recognition failed")),
            }
            if let Some(left)=Duration::from_millis(200).checked_sub(started.elapsed()){std::thread::sleep(left);}
        }
    }
    let stream=match ws.get_mut(){MaybeTlsStream::Plain(s)=>s,MaybeTlsStream::Rustls(s)=>&mut s.sock,_=>return Err(error("Unsupported TLS"))};
    stream.set_read_timeout(Some(Duration::from_secs(30)))?;
    loop { match ws.read().map_err(|_|error("Doubao recognition failed or timed out"))? {
        Message::Binary(b)=>{let (last,new)=response(&b)?;if new.is_some(){text=new;}if last {break;}},
        Message::Close(_)=>return Err(error("Doubao closed before final result")),_=>{}
    }}
    text.ok_or_else(||error("Empty recognition result"))
}

#[cfg(test)]
mod tests {
 use super::*;
 #[test] fn exchanges_audio_and_final_result_over_websocket() {
  let listener=std::net::TcpListener::bind("127.0.0.1:0").unwrap();let addr=listener.local_addr().unwrap();
  let server=std::thread::spawn(move||{
   let (stream,_)=listener.accept().unwrap();stream.set_read_timeout(Some(Duration::from_secs(4))).unwrap();
   let mut ws=tungstenite::accept_hdr(stream,|req:&tungstenite::handshake::server::Request,res:tungstenite::handshake::server::Response|{
    assert_eq!(req.headers()["X-Api-Key"],"test-key");assert_eq!(req.headers()["X-Api-Resource-Id"],"volc.seedasr.sauc.duration");Ok(res)
   }).unwrap();
   let first=ws.read().unwrap().into_data();assert_eq!(first[1],0x11);
   let mut reply=vec![0x11,0x91,0x10,0];reply.extend(1i32.to_be_bytes());reply.extend(2u32.to_be_bytes());reply.extend(b"{}");ws.send(Message::Binary(reply)).unwrap();
   let audio=ws.read().unwrap().into_data();assert_eq!(audio[1],0x21);
   let last=ws.read().unwrap().into_data();assert_eq!(last[1],0x23);
   let payload=r#"{"result":{"text":"中文 test"}}"#.as_bytes();
   let mut reply=vec![0x11,0x93,0x10,0];reply.extend((-2i32).to_be_bytes());reply.extend((payload.len() as u32).to_be_bytes());reply.extend(payload);ws.send(Message::Binary(reply)).unwrap();
  });
  let text=transcribe_at("test-key",&vec![0u8;7000],&format!("ws://{addr}/test")).unwrap();
  assert_eq!(text,"中文 test");server.join().unwrap();
 }
 #[test] fn parses_compressed_response_and_rejects_bad_frames() {
  let payload=br#"{\"result\":{\"text\":\"hello\"}}"#;
  // Build server message with final sequence flag.
  let mut gz=GzEncoder::new(Vec::new(),Compression::default());
  gz.write_all(payload).unwrap(); let gz=gz.finish().unwrap();
  let mut frame=vec![0x11,0x93,0x11,0];frame.extend((-2i32).to_be_bytes());frame.extend((gz.len() as u32).to_be_bytes());frame.extend(gz);
  // A valid JSON body is required.
  assert!(response(&frame).is_err());
  let payload=r#"{"result":{"text":"中文 Hello123"}}"#.as_bytes();
  let mut gz=GzEncoder::new(Vec::new(),Compression::default());gz.write_all(payload).unwrap();let gz=gz.finish().unwrap();
  let mut frame=vec![0x11,0x93,0x11,0];frame.extend((-2i32).to_be_bytes());frame.extend((gz.len() as u32).to_be_bytes());frame.extend(gz);
  assert_eq!(response(&frame).unwrap(),(true,Some("中文 Hello123".to_owned())));
  frame.pop();assert!(response(&frame).is_err());assert!(response(&[0x11,0xf0,0,0]).is_err());
 }
}
