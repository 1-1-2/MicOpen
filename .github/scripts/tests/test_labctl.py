"""连接串解析、主机信任与双机会话截止时间的回归。"""
import datetime as dt
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import labctl
import lab_relay
import lab_session

class RelayTests(unittest.TestCase):
    def connection(self):
        return dict(host='100.64.1.2',user='runneradmin',port=22,host_keys=['ssh-ed25519 AAAA test'],relay=dict(kind='upterm',version=lab_relay.VERSION,host='uptermd.upterm.dev',port=443,user='session:Ym9i=',ca=lab_relay.CA))
    def test_preparation_scenes_reject_test_phase_pause(self):
        for scenario in ['base','toolchain','built','installed']:
            data=dict(scenario=scenario,target='windows',session_minutes='5',artifact_run_id='0',pause_at='after-test',stream_logs=False)
            with patch.dict(os.environ,{'LAB_INPUTS':json.dumps(data)}):
                with self.assertRaisesRegex(ValueError,'pause_at applies'):labctl.plan()
    def test_integer_inputs_accept_api_strings(self):
        self.assertEqual(labctl.integer_input('40491670551','run',0,2**63-1),40491670551)
        self.assertEqual(labctl.integer_input('5','minutes',1,120),5)
        self.assertEqual(labctl.integer_input(20,'minutes',1,120),20)
    def test_integer_inputs_reject_noninteger_and_out_of_range(self):
        for value in ['1.5','-1','nan','5;echo bad',121,True]:
            with self.assertRaises(ValueError):labctl.integer_input(value,'minutes',1,120)
    def test_dispatch_transmits_form_values_as_strings(self):
        with patch.object(labctl,'api',return_value={'workflow_run_id':123}) as api:
            labctl.dispatch('lab.yml','main',['artifact_run_id=40491670551','session_minutes=5','stream_logs=false'])
        inputs=api.call_args.args[2]['inputs']
        self.assertEqual(inputs['artifact_run_id'],'40491670551')
        self.assertEqual(inputs['session_minutes'],'5')
        self.assertEqual(inputs['stream_logs'],'false')
    def test_wss_scheme_is_not_part_of_ssh_user(self):
        self.assertEqual(lab_relay.relay_user("ssh -o 'ProxyCommand=upterm proxy wss://session:Ym9i=@uptermd.upterm.dev' session:Ym9i=@uptermd.upterm.dev:443"),'session:Ym9i=')
        with self.assertRaises(ValueError):lab_relay.relay_user('wss://session@untrusted.example')
    def test_relay_certificate_pin_is_mandatory(self):
        with tempfile.TemporaryDirectory() as d:
            c=self.connection();c['relay']['ca']='ssh-ed25519 OTHER'
            with self.assertRaises(ValueError):labctl.ssh_options(c,None,Path(d)/'host',Path(d)/'relay')
    def test_user_cannot_inject_shell_command(self):
        with tempfile.TemporaryDirectory() as d:
            for bad in ['wss://session','session;echo BAD','$(echo BAD)','session%h']:
                c=self.connection();c['relay']['user']=bad
                with self.assertRaises(ValueError):labctl.ssh_options(c,None,Path(d)/'host',Path(d)/'relay')
    @unittest.skipIf(os.name=='nt','local OpenSSH config parser is checked on Linux')
    def test_exported_config_is_accepted_by_openssh(self):
        with tempfile.TemporaryDirectory(prefix='miclink space ') as d:
            c=self.connection();c['deadline_epoch']=9999999999
            dest=Path(d)/'ssh.conf';identity=Path(d)/'my key'
            labctl.write_ssh_config(c,str(identity),dest)
            p=subprocess.run(['ssh','-G','-F',str(dest),'miclink-lab'],capture_output=True,text=True,check=True)
            self.assertIn('stricthostkeychecking true',p.stdout)
            self.assertIn('proxycommand ssh',p.stdout)
            self.assertIn('127.0.0.1:22',p.stdout)
            self.assertIn(str(identity),p.stdout)
    def test_dual_readiness_updates_connection_deadline(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'connection').mkdir()
            own=dict(run_id='123',attempt='2',role='linux-app',ready_at='2026-10-27T10:00:00+00:00',session_minutes=20,deadline_epoch=0)
            peer=dict(run_id='123',attempt='2',role='windows-client',ready_at='2026-10-27T10:03:00+00:00',session_minutes=20)
            lab_session.atomic(root/'state.json',own);lab_session.atomic(root/'connection/connection.json',dict(deadline_epoch=0))
            b=io.BytesIO()
            with zipfile.ZipFile(b,'w') as z:z.writestr('ready.json',json.dumps(peer))
            def api(path,**kwargs):
                if path.endswith('/zip'):return b.getvalue()
                return {'artifacts':[{'id':1,'name':'readiness-123-2-windows-client','expired':False}]}
            with patch.object(lab_session,'root_for',return_value=root),patch.object(labctl,'api',side_effect=api):labctl.barrier('ready')
            expected=dt.datetime.fromisoformat('2026-10-27T10:23:00+00:00').timestamp()
            self.assertEqual(lab_session.state(root)['deadline_epoch'],expected)
            self.assertEqual(json.loads((root/'connection/connection.json').read_text())['deadline_epoch'],expected)

if __name__=='__main__':unittest.main()
