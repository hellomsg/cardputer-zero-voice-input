#!/usr/bin/env python3
"""Exercise the Rust broker against a native mock, without touching live services."""
import json, os, socket, subprocess, tempfile, threading, time
from pathlib import Path

class FakeAdapter:
    def __init__(self,path):
        self.path=path;self.received=[];self.stop=False
        self.sock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);self.sock.bind(str(path));self.sock.listen()
        self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start()
    def run(self):
        self.sock.settimeout(.1)
        while not self.stop:
            try: conn,_=self.sock.accept()
            except socket.timeout: continue
            with conn:
                data=b""
                while not data.endswith(b"\n"):
                    chunk=conn.recv(8192)
                    if not chunk: break
                    data+=chunk
                req=json.loads(data)
                if req.get('op')=='capture': response={'ok':True,'token':'generation-1'}
                elif req.get('op')=='commit' and req.get('token')=='generation-1':
                    self.received.append(req['text']);response={'ok':True}
                else: response={'ok':False}
                conn.sendall(json.dumps(response).encode()+b"\n")
    def close(self):
        self.stop=True;self.thread.join(1);self.sock.close();self.path.unlink()

stage=Path(os.environ.get('CARDPUTER_TEST_STAGE',Path.home()/'.local/share/voice-input-staging'))
with tempfile.TemporaryDirectory(prefix='rust-broker-test-') as tmp:
    root=Path(tmp); out=root/'out'; invalidate=root/'invalidate'
    env=dict(os.environ, XDG_RUNTIME_DIR=tmp, MOCK_OUTPUT=str(out), MOCK_INVALIDATE=str(invalidate), MOCK_TERMINAL='1')
    mock_env=dict(env,LD_PRELOAD=str(stage/'shared_bridge_test.so'))
    mock=subprocess.Popen([str(stage/'mock_lvgl')],env=mock_env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    broker=subprocess.Popen([str(stage/'rust/target/release/text')],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        path=root/'voice-input/text.sock'
        for _ in range(200):
            if path.exists() and list(path.parent.glob('adapter-*.native')): break
            if broker.poll() is not None or mock.poll() is not None: raise RuntimeError('test process exited')
            time.sleep(.05)
        def req(obj):
            with socket.socket(socket.AF_UNIX) as s:
                s.settimeout(4);s.connect(str(path));s.sendall(json.dumps(obj,ensure_ascii=False).encode()+b'\n')
                data=b''
                while not data.endswith(b'\n'): data+=s.recv(8192)
                return json.loads(data)
        second=subprocess.Popen([str(stage/'mock_lvgl')],env=mock_env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            for _ in range(100):
                if len(list(path.parent.glob('adapter-*.native')))==2: break
                time.sleep(.05)
            assert not req({'op':'capture'})['ok'], 'multiple focused targets must fail closed'
        finally:
            second.terminate(); second.wait(timeout=4)
        token=req({'op':'capture'})['token']; assert not req({'op':'commit','token':token,'text':'not a command\n'})['ok']; assert not out.exists()
        token=req({'op':'capture'})['token']; assert req({'op':'check','token':token})['ok']; assert req({'op':'commit','token':token,'text':'中文，A1'})['ok']; assert out.read_text()=='中文，A1'
        assert not req({'op':'commit','token':token,'text':'再次'})['ok']
        token=req({'op':'capture'})['token'];invalidate.touch();time.sleep(.1)
        assert not req({'op':'check','token':token})['ok'];assert not req({'op':'commit','token':token,'text':'禁止输入'})['ok'];assert out.read_text()=='中文，A1'
        mock.terminate();mock.wait(timeout=4)
        adapter=FakeAdapter(path.parent/'adapter-123.sock')
        try:
            token=req({'op':'capture'})['token'];assert req({'op':'commit','token':token,'text':'扩展 A1'})['ok']
            assert adapter.received==['扩展 A1']
        finally: adapter.close()
        print('Rust broker: native and JSON adapters, ambiguous targets, newline rejection, Unicode, single-use token and changed-focus rejection passed')
    finally:
        for p in (broker,mock):
            if p.poll() is None: p.terminate()
        for p in (broker,mock): p.wait(timeout=4)
        broker.stderr.close()
