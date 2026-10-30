import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / 'direct-avd.py'
spec = importlib.util.spec_from_file_location('direct_avd', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DirectAvdReportTests(unittest.TestCase):
    def test_official_image_package_versions(self):
        self.assertEqual(module.image_package(35), 'system-images;android-35;google_apis;x86_64')
        self.assertEqual(module.image_package(37), 'system-images;android-37.0;google_apis;x86_64')
    def test_logcat_prefix_and_unrelated_lines(self):
        text = '10-30 12:00:00 I DirectProbe: {"event":"client_ready","formed":true}\nnot JSON\n{"other":"ignored"}'
        self.assertEqual(module.events(text), [{'event': 'client_ready', 'formed': True}])
    def test_partial_json_cannot_be_counted_as_success(self):
        self.assertEqual(module.events('I DirectProbe: {"event":"passed"'), [])
    def test_error_and_success_remain_separate_events(self):
        self.assertEqual([e['event'] for e in module.events('{"event":"error"}\n{"event":"passed"}')], ['error', 'passed'])
    def test_google_wifi_invitation(self):
        xml = '<?xml version="1.0"?><hierarchy><node package="com.google.android.wifi.dialog" text="Invitation to connect"/><node package="com.google.android.wifi.dialog" resource-id="android:id/button1" enabled="true" bounds="[20,100][100,140]"/></hierarchy>'
        self.assertEqual(module.p2p_accept_bounds(xml), (60,120))
    def test_arbitrary_positive_button_is_not_accepted(self):
        xml = '<?xml version="1.0"?><hierarchy><node package="android" text="Delete everything?"/><node package="android" resource-id="android:id/button1" bounds="[0,0][10,10]"/></hierarchy>'
        self.assertIsNone(module.p2p_accept_bounds(xml))
    def test_untrusted_application_cannot_impersonate_invitation(self):
        xml = '<?xml version="1.0"?><hierarchy><node package="com.example.app" text="Invitation to connect" resource-id="android:id/button1" bounds="[0,0][10,10]"/></hierarchy>'
        self.assertIsNone(module.p2p_accept_bounds(xml))


if __name__ == '__main__': unittest.main()
