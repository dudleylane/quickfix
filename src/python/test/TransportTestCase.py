import os
import socket
import subprocess
import sys
import tempfile
import unittest


# Runs TransportChild.py in a process of its own -- an acceptor and an initiator
# logged on to each other, with a Python Application, LogFactory and Log -- so a
# crash or a hang there fails one test rather than taking the suite down.
class TransportTestCase(unittest.TestCase):

    def test_socket_stopped(self):
        self.checkTransports('socket', 'stop')

    def test_socket_left_running(self):
        self.checkTransports('socket', 'exit')

    def test_threaded_stopped(self):
        self.checkTransports('threaded', 'stop')

    def test_threaded_left_running(self):
        self.checkTransports('threaded', 'exit')

    # The classes SWIG generates kept nothing they were made with (#110).
    def test_base_stopped(self):
        self.checkTransports('base', 'stop')

    def test_base_left_running(self):
        self.checkTransports('base', 'exit')

    def test_threaded_base_stopped(self):
        self.checkTransports('threadedbase', 'stop')

    def test_threaded_base_left_running(self):
        self.checkTransports('threadedbase', 'exit')

    # TLS on both transports; no binding test used SSL before #139.
    def test_ssl_stopped(self):
        self.checkTransports('ssl', 'stop')

    def test_ssl_left_running(self):
        self.checkTransports('ssl', 'exit')

    def test_threaded_ssl_stopped(self):
        self.checkTransports('threadedssl', 'stop')

    def test_threaded_ssl_left_running(self):
        self.checkTransports('threadedssl', 'exit')

    def checkTransports(self, kind, ending):
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        child = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'TransportChild.py')
        with tempfile.TemporaryDirectory() as dir:
            try:
                result = subprocess.run([sys.executable, child, dir, str(port), kind, ending],
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
                output, status = result.stdout, result.returncode
            except subprocess.TimeoutExpired as expired:
                output, status = (expired.stdout or b'').decode(errors='replace'), 'timed out'
        lines = dict(line.split('=', 1) for line in output.splitlines() if '=' in line)

        # A LogFactory written in Python aborted here, in SWIG's director mutex (#109).
        self.assertEqual('true', lines.get('constructed'), output)
        self.assertEqual('true', lines.get('started'), output)
        self.assertEqual('True', lines.get('loggedOn'), output)
        # A message toApp refuses with DoNotSend is not sent, and sendToTarget says so.
        self.assertEqual('True', lines.get('refused'), output)
        self.assertEqual('sent,after', lines.get('received'), output)
        # The Log is called from the transport's threads.
        self.assertEqual('True', lines.get('logged'), output)
        # Another Python thread ran while the transports did.
        self.assertEqual('True', lines.get('ticked'), output)
        if ending == 'stop':
            self.assertEqual('True', lines.get('stopped'), output)
        # Including when the interpreter's exit is left to free running transports.
        self.assertEqual(0, status, output)


if __name__ == '__main__':
    unittest.main()
