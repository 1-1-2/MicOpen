"""与公开附件边界、失败判定、进程预算相关的回归。"""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(SCRIPTS))
import ci
import labctl

class ArtifactTests(unittest.TestCase):
    def test_android_stages_both_variants_in_separate_directories(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            products={
                'android/app/build/outputs/apk/debug/app-debug.apk':b'debug-apk',
                'android/app/build/outputs/apk/release/app-release.apk':b'release-apk',
                'android/app/build/outputs/mapping/release/mapping.txt':b'Class -> a:\n',
                'android/app/build/outputs/release-info.json':b'{"minify":true}\n',
                'android/app/src/main/cpp/opus/COPYING':b'Opus license',
                'LICENSE':b'Project license','NOTICE.md':b'Notices',
            }
            for name,data in products.items():
                p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
            with patch.object(ci,'ROOT',root),patch.object(ci.subprocess,'check_output',return_value='fake-sha\n'):
                ci.stage('android')
            out=root/'_public/android';m=json.loads((out/'manifest.json').read_text())
            self.assertEqual(m['variants'],['debug','release'])
            self.assertEqual((out/'release/app-release.apk').read_bytes(),b'release-apk')
            self.assertTrue((out/'debug/app-debug.apk').is_file())
            self.assertFalse((out/'app-debug.apk').exists())
            self.assertTrue((out/'release/mapping.txt').is_file())
    def test_embedded_driver_is_rejected_inside_exe(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'womic-drvinst.exe';p.write_bytes(b'MZ'+b'padding'*20+b'MICLDRV1'+struct.pack('<II',1,9)+b'compressed driver')
            with self.assertRaises(ValueError):ci.safe_exe(p)
            p.write_bytes(b'MZ'+b'MICLDRV1'+struct.pack('<II',1,0));ci.safe_exe(p)
    def test_hash_mismatch_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'a.exe').write_bytes(b'MZbad')
            (p/'manifest.json').write_text(json.dumps({'sha':'abc','embedded_drivers':False,'files':[{'path':'a.exe','sha256':'wrong'}]}))
            with self.assertRaises(ValueError):ci.verify(p)
    def test_path_traversal_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'manifest.json').write_text(json.dumps({'sha':'abc','embedded_drivers':False,'files':[{'path':'../x','sha256':'wrong'}]}))
            with self.assertRaises(ValueError):ci.verify(p)
    def test_required_skipped_job_does_not_pass(self):
        needs={'plan':{'result':'success','outputs':{'android':'false','windows':'true','verify':'true'}},'checks':{'result':'success'},'build-windows':{'result':'success'},'verify-windows':{'result':'skipped'}}
        with patch.dict(os.environ,{'NEEDS_JSON':json.dumps(needs)}):self.assertTrue(ci.summary())
    def test_selected_lab_scene_cannot_be_skipped(self):
        needs={'plan':{'result':'success','outputs':{'dual':'false'}},'scene':{'result':'skipped'},'linux-app':{'result':'skipped'},'windows-client':{'result':'skipped'}}
        with patch.dict(os.environ,{'NEEDS_JSON':json.dumps(needs)}):self.assertTrue(ci.summary())
        needs['plan']['outputs']['dual']='true';needs['linux-app']['result']='success'
        with patch.dict(os.environ,{'NEEDS_JSON':json.dumps(needs)}):self.assertTrue(ci.summary())
        needs['windows-client']['result']='success'
        with patch.dict(os.environ,{'NEEDS_JSON':json.dumps(needs)}):self.assertFalse(ci.summary())
    def test_deadline_kills_process(self):
        self.assertEqual(ci.run(.05,[sys.executable,'-c','import time;time.sleep(10)']),124)
    def test_invalid_lab_combinations_fail_before_runner_allocation(self):
        data={'scenario':'installed','target':'linux','session_minutes':20,'artifact_run_id':0,'pause_at':'ready','stream_logs':False}
        with patch.dict(os.environ,{'LAB_INPUTS':json.dumps(data)}):
            with self.assertRaises(ValueError):labctl.plan()
    def test_declared_public_report_does_not_include_random_files(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'probe-results').mkdir();(root/'probe-results/private-note.txt').write_text('DO NOT UPLOAD')
            (root/'probe-results/capture.wav').write_bytes(b'personal audio')
            (root/'probe-results/record').mkdir();(root/'probe-results/record/personal-note.json').write_text('{\"private\":true}')
            (root/'probe-results/personal-verdict.json').write_text('{\"private\":true}')
            (root/'probe-results/verdict.txt').write_text('RESULT=PASS')
            with patch.object(ci,'ROOT',root),patch.object(ci.subprocess,'check_output',return_value='fake-sha\n'):
                ci.stage('lab-report')
            out=root/'_public/lab-report'
            self.assertTrue((out/'verdict.txt').exists());self.assertFalse((out/'private-note.txt').exists());self.assertFalse((out/'capture.wav').exists());self.assertFalse((out/'record/personal-note.json').exists());self.assertFalse((out/'personal-verdict.json').exists())

if __name__=='__main__':unittest.main()
