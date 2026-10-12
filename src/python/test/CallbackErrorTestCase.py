import os
import re
import socket
import subprocess
import sys
import tempfile
import unittest


# An exception a Python callback lets out, other than the FIX exceptions the
# engine expects, ends the process with status 1 and the traceback printed as
# Python prints an uncaught one; a SystemExit ends it with its own code. On the
# reactor transports it used to crash the process (#122). The child runs in a
# process of its own.
class CallbackErrorTestCase(unittest.TestCase):

    def test_application_callback(self):
        self.checkExit('socket', 'onLogon', 1, 'ValueError: raised in onLogon')

    def test_application_callback_threaded(self):
        self.checkExit('threaded', 'onLogon', 1, 'ValueError: raised in onLogon')

    def test_initiator_callback(self):
        self.checkExit('socket', 'initiator', 1, 'ValueError: raised in onLogon', runs=5)

    def test_initiator_callback_threaded(self):
        self.checkExit('threaded', 'initiator', 1, 'ValueError: raised in onLogon', runs=5)

    def test_log(self):
        self.checkExit('socket', 'log', 1, 'ValueError: raised in Log.onIncoming')

    def test_log_threaded(self):
        self.checkExit('threaded', 'log', 1, 'ValueError: raised in Log.onIncoming')

    def test_system_exit(self):
        self.checkExit('socket', 'exit', 3, None)

    def test_system_exit_threaded(self):
        self.checkExit('threaded', 'exit', 3, None)

    def checkExit(self, kind, where, status, message, runs=1):
        # The crash was a race with the engine's thread, so a case that lost it
        # only some of the time runs more than once.
        for run in range(runs):
            self.checkOneExit(kind, where, status, message)

    def checkOneExit(self, kind, where, status, message):
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        child = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'CallbackErrorChild.py')
        with tempfile.TemporaryDirectory() as dir:
            try:
                result = subprocess.run([sys.executable, child, dir, str(port), kind, where],
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=60)
                returncode, stdout, stderr = result.returncode, result.stdout, result.stderr
            except subprocess.TimeoutExpired as expired:
                returncode = 'timed out'
                stdout = (expired.stdout or b'').decode(errors='replace')
                stderr = (expired.stderr or b'').decode(errors='replace')
        report = f'{returncode}\nstdout:\n{stdout}\nstderr:\n{stderr}'

        self.assertEqual(status, returncode, report)
        if message:
            self.assertIn(message, stderr, report)
            self.assertRegex(stderr, r'Traceback \(most recent call last\):', report)
            self.assertRegex(stderr, r'CallbackErrorChild\.py", line \d+', report)
        # Python's own stdout buffer was flushed on the way out.
        self.assertRegex(stdout, re.compile(r'^buffered=true$', re.M), report)
        self.assertNotRegex(stdout, re.compile(r'^survived=', re.M), report)


if __name__ == '__main__':
    unittest.main()
