import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / 'direct-avd.py'
spec = importlib.util.spec_from_file_location('direct_avd', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DirectAvdReportTests(unittest.TestCase):
    def test_logcat_prefix_and_unrelated_lines(self):
        text = '10-30 12:00:00 I DirectProbe: {"event":"client_ready","formed":true}\nnot JSON\n{"other":"ignored"}'
        self.assertEqual(module.events(text), [{'event': 'client_ready', 'formed': True}])
    def test_partial_json_cannot_be_counted_as_success(self):
        self.assertEqual(module.events('I DirectProbe: {"event":"passed"'), [])
    def test_error_and_success_remain_separate_events(self):
        self.assertEqual([e['event'] for e in module.events('{"event":"error"}\n{"event":"passed"}')], ['error', 'passed'])


if __name__ == '__main__': unittest.main()
