import json, os, socket, subprocess, tempfile, time, unittest
from pathlib import Path
HERE=Path(__file__).resolve().parent

class ProtocolError(Exception):
    pass

class Broker:
    def __init__(self, path):
        self.path=path
    def dispatch(self, request):
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(4); client.connect(str(self.path))
            client.sendall(json.dumps(request,ensure_ascii=False).encode()+b"\n")
            response=b""
            while not response.endswith(b"\n"):
                data=client.recv(8192)
                if not data: raise ProtocolError("Broker closed connection")
                response+=data
            result=json.loads(response)
            if not result.get("ok"): raise ProtocolError(result.get("error","Request failed"))
            return result

class NativeTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup); root=Path(self.tmp.name)
        self.runtime=root/'voice-input'; self.output=root/'output'; self.invalid=root/'invalidate'
        self.badge_created=root/'badge-created'; self.badge_text=root/'badge-text'; self.badge_pos=root/'badge-pos'
        env=dict(os.environ,XDG_RUNTIME_DIR=str(root),MOCK_OUTPUT=str(self.output),MOCK_INVALIDATE=str(self.invalid),MOCK_BADGE_CREATED=str(self.badge_created),MOCK_BADGE_TEXT=str(self.badge_text),MOCK_BADGE_POS=str(self.badge_pos),LD_PRELOAD=str(HERE/os.environ.get('VOICE_BRIDGE_TEST','shared_bridge_test.so')))
        if os.environ.get('VOICE_TERMINAL_TEST'): env['MOCK_TERMINAL']='1'
        self.proc=subprocess.Popen([str(HERE/'mock_lvgl')],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        self.addCleanup(self.cleanup_process)
        broker_path=Path(os.environ.get('CARDPUTER_TEXT_BIN',HERE/'rust/target/release/text')).resolve()
        self.broker_proc=subprocess.Popen([str(broker_path)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        self.addCleanup(self.cleanup_broker)
        self.path=self.runtime/('adapter-%d.native'%self.proc.pid)
        for _ in range(150):
            if self.path.exists() and (self.runtime/'text.sock').exists(): break
            if self.proc.poll() is not None or self.broker_proc.poll() is not None: self.fail('mock or Rust broker exited')
            time.sleep(.02)
        self.assertTrue(self.path.exists(),'adapter did not start')
    def cleanup_broker(self):
        self.broker_proc.terminate(); self.broker_proc.wait(timeout=3); self.broker_proc.stderr.close()
    def cleanup_process(self):
        self.proc.terminate(); self.proc.wait(timeout=3); self.proc.stderr.close()
    def test_control_characters_rejected_for_terminal(self):
        if not os.environ.get('VOICE_TERMINAL_TEST'): self.skipTest('terminal profile only')
        broker=Broker(self.runtime/'text.sock'); token=broker.dispatch({'op':'capture'})['token']
        with self.assertRaises(ProtocolError): broker.dispatch({'op':'commit','token':token,'text':'do not execute\n'})
        self.assertFalse(self.output.exists())
    def test_unicode_delivery_and_changed_focus(self):
        broker=Broker(self.runtime/'text.sock'); first=broker.dispatch({'op':'capture'})['token']
        broker.dispatch({'op':'commit','token':first,'text':'你好，世界! A1'})
        self.assertEqual(self.output.read_text(),'你好，世界! A1')
    def test_provider_badge_shows_on_right_without_affecting_focus(self):
        broker=Broker(self.runtime/'text.sock'); token=broker.dispatch({'op':'capture'})['token']
        status=self.runtime/'status.json'
        for state,text in (('provider_openai','OpenAI'),('provider_deepseek_missing','DeepSeek !'),('provider_none','No ASR key')):
            status.write_text(json.dumps({'state':state},separators=(',',':')))
            time.sleep(.5)
            self.assertEqual(self.badge_text.read_text(),text)
            self.assertEqual(self.badge_pos.read_text(),'208,2')
        broker.dispatch({'op':'commit','token':token,'text':'测试'})
        self.assertEqual(self.output.read_text(),'测试')

    def test_badge_preserves_target_identity(self):
        broker=Broker(self.runtime/'text.sock'); token=broker.dispatch({'op':'capture'})['token']
        status=self.runtime/'status.json'
        status.write_text(json.dumps({'state':'recording'},separators=(',',':')))
        time.sleep(.4)
        self.assertTrue(self.badge_created.exists())
        status.write_text(json.dumps({'state':'transcribing'},separators=(',',':')))
        time.sleep(.4)
        for state, text in (('success', 'OK'), ('failure', 'FAIL')):
            status.write_text(json.dumps({'state':state},separators=(',',':')))
            time.sleep(.4)
            self.assertEqual(self.badge_text.read_text(), text)
            self.assertEqual(self.badge_pos.read_text(), '2,2')
        broker.dispatch({'op':'commit','token':token,'text':'状态显示期间输入'})
        self.assertEqual(self.output.read_text(),'状态显示期间输入')
        second=broker.dispatch({'op':'capture'})['token']
        self.invalid.touch(); time.sleep(.1)
        with self.assertRaises(ProtocolError): broker.dispatch({'op':'commit','token':second,'text':'不可插入'})
        self.assertEqual(self.output.read_text(),'状态显示期间输入')
if __name__=='__main__': unittest.main()
