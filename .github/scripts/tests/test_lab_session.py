"""会话正文开关、实时读取、退出码、分块脱敏和结束行为。"""
import importlib.util
import io
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest

SCRIPT=Path(__file__).resolve().parents[1]/'lab_session.py'
spec=importlib.util.spec_from_file_location('lab_session',SCRIPT);lab=importlib.util.module_from_spec(spec);spec.loader.exec_module(lab)

class SessionTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)/'session'
    def tearDown(self):self.temp.cleanup()
    def call(self,*args,check=True):
        env=dict(os.environ)
        env.pop('GITHUB_ENV',None);env.pop('GITHUB_STEP_SUMMARY',None)
        return subprocess.run([sys.executable,str(SCRIPT),'--root',str(self.root),*args],capture_output=True,text=True,encoding='utf-8',check=check,env=env)
    def start(self,stream):
        self.call('init','--minutes','1','--stream',str(stream).lower());self.call('ready','--host','127.0.0.1')
    def test_off_has_no_body_or_transcript(self):
        self.start(False)
        p=self.call('exec','--',sys.executable,'-c','print("PRIVATE_BODY_123")')
        self.assertIn('PRIVATE_BODY_123',p.stdout)
        with lab.database(self.root) as db:self.assertEqual(db.execute('select count(*) from events').fetchone()[0],0)
        self.call('export');self.assertFalse((self.root/'public-report/public-session.log').exists())
    def test_exit_code_and_both_streams(self):
        self.start(True)
        p=self.call('exec','--',sys.executable,'-c','import sys;print("hello");print("err",file=sys.stderr);sys.exit(7)',check=False)
        self.assertEqual(p.returncode,7)
        with lab.database(self.root) as db:rows=db.execute('select stream,text from events').fetchall()
        self.assertIn(('stdout','hello'),rows);self.assertIn(('stderr','err'),rows);self.assertIn(('exit','7'),rows)
    def test_token_split_across_writes_and_no_newline(self):
        self.start(True)
        self.call('exec','--',sys.executable,'-c','import os;os.write(1,b"github_");os.write(1,b"pat_DEMOONLY12345678901234567890");os.write(1,b"\\nlast")')
        self.call('export');s=(self.root/'public-report/public-session.log').read_text()
        self.assertNotIn('pat_DEMOONLY',s);self.assertIn('[TOKEN REDACTED]',s);self.assertIn('last',s)
    def test_pem_state_and_unicode(self):
        r=lab.Redactor()
        self.assertEqual(r.line('-----BEGIN OPENSSH PRIVATE KEY-----'),'[PRIVATE KEY WITHHELD]')
        self.assertEqual(r.line('sensitive base64 payload'),'[PRIVATE KEY WITHHELD]')
        self.assertEqual(r.line('-----END OPENSSH PRIVATE KEY-----'),'[PRIVATE KEY WITHHELD]')
        self.assertEqual(r.line('中文正常'),'中文正常')
    def test_stream_before_finish_and_normal_release(self):
        self.start(True)
        proc=subprocess.Popen([sys.executable,'-u',str(SCRIPT),'--root',str(self.root),'serve','--seconds','15'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
        q=queue.Queue()
        t=threading.Thread(target=lambda:[q.put((time.monotonic(),x)) for x in proc.stdout],daemon=True);t.start()
        try:
            limit=time.monotonic()+5
            while time.monotonic()<limit:
                if 'WAITING' in q.get(timeout=5)[1]:break
            start=time.monotonic()
            self.call('exec','--',sys.executable,'-c','print("LIVE_SENTINEL")')
            found=False
            while time.monotonic()-start<4:
                at,line=q.get(timeout=4)
                if 'stdout] LIVE_SENTINEL' in line:
                    found=True;self.assertLess(at-start,3);break
            self.assertTrue(found);self.assertIsNone(proc.poll())
            self.call('finish');self.assertEqual(proc.wait(timeout=5),0)
            self.assertEqual(lab.state(self.root)['phase'],'RELEASED')
        finally:
            if proc.poll() is None:proc.kill();proc.wait()
            t.join(timeout=2);proc.stdout.close();proc.stderr.close()
    def test_remote_exec_inherits_prepared_job_context(self):
        self.start(False)
        runtime=json.loads((self.root/'runtime.json').read_text())
        runtime['cwd']=str(self.root);runtime['env']['CONTROL_PORT']='8125'
        (self.root/'runtime.json').write_text(json.dumps(runtime))
        p=self.call('exec','--','python','-c','import os;print(os.getcwd());print(os.environ["CONTROL_PORT"])')
        self.assertIn(str(self.root),p.stdout);self.assertIn('8125',p.stdout)
        self.assertNotIn('GH_TOKEN',runtime['env'])
    def test_invalid_duration_and_early_execution(self):
        self.assertNotEqual(self.call('init','--minutes','121',check=False).returncode,0)
        self.call('init');self.assertNotEqual(self.call('exec','--',sys.executable,'-c','print(1)',check=False).returncode,0)
    def test_truncated_binary_sized_line_withheld(self):
        self.start(True);self.call('exec','--',sys.executable,'-c','print("x"*40000)');self.call('export')
        s=(self.root/'public-report/public-session.log').read_text();self.assertIn('OVERSIZED LINE WITHHELD',s);self.assertNotIn('x'*100,s)
    def test_running_command_is_bounded_by_session_deadline(self):
        for stream in [False,True]:
            self.root=Path(self.temp.name)/('bounded-'+str(stream))
            self.start(stream)
            info=lab.state(self.root);info['deadline_epoch']=time.time()+.4;lab.atomic(self.root/'state.json',info)
            start=time.monotonic()
            p=self.call('exec','--',sys.executable,'-c','import time;time.sleep(10)',check=False)
            self.assertEqual(p.returncode,124);self.assertLess(time.monotonic()-start,4)
    def test_finish_stops_running_command(self):
        self.start(False)
        def end():
            time.sleep(.3);self.call('finish')
        t=threading.Thread(target=end);t.start()
        try:
            p=self.call('exec','--',sys.executable,'-c','import time;time.sleep(10)',check=False)
            self.assertEqual(p.returncode,130)
        finally:t.join()
    def test_expiry_distinct_from_product_pass(self):
        self.start(False);self.call('serve','--seconds','.05');self.assertEqual(lab.state(self.root)['phase'],'EXPIRED')

if __name__=='__main__':unittest.main()
