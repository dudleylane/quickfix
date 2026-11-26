import os
import subprocess
import sys
import unittest


# A SynchronizedApplication keeps the application it wraps (#208), so an application dropped by the script is
# still reached through the wrapper rather than used from freed memory. Runs in a process of its own, so that a
# crash is a failure here.
class SynchronizedApplicationTestCase(unittest.TestCase):

    def test_wrapped_application_survives_collection(self):
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'SynchronizedApplicationChild.py')
        result = subprocess.run([sys.executable, script], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=60)
        self.assertEqual(0, result.returncode, result.stdout)
        self.assertNotIn('Sanitizer', result.stdout)
        lines = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
        self.assertEqual('true', lines.get('end'), result.stdout)


if __name__ == '__main__':
    unittest.main()
