import os
import subprocess
import sys
import unittest


# Runs TransportGcChild.py in a process of its own, once for each way a transport and the factories it was made with
# go together, so that a crash is a failure here rather than the end of the suite (#201).
class TransportGcTestCase(unittest.TestCase):

    def child(self, mode):
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'TransportGcChild.py')
        result = subprocess.run([sys.executable, script, mode], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=60)
        self.assertEqual(0, result.returncode, result.stdout)
        self.assertNotIn('Sanitizer', result.stdout)
        lines = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
        self.assertEqual('true', lines.get('end'), result.stdout)
        return lines, result.stdout

    def collected(self, mode):
        lines, output = self.child(mode)
        # Held from the transport's construction until its C++ object was deleted, and only until then.
        self.assertEqual('1', lines.get('pinned'), output)
        self.assertEqual('0', lines.get('pins'), output)
        # Deleted, so its sessions are unregistered.
        self.assertEqual('False', lines.get('registered'), output)
        return lines, output

    def test_acceptor(self):
        self.collected('acceptor')

    def test_initiator(self):
        self.collected('initiator')

    def test_started_and_stopped(self):
        self.collected('stopped')

    def test_log_factory_written_in_python(self):
        lines, output = self.collected('scriptlogs')
        # The transport's own log and its session's, each destroyed through the factory, which was still alive.
        self.assertEqual('2', lines.get('destroyed'), output)

    # The application is not pinned (#204): one that refers to its transport is still collected with it.
    def test_application_referring_to_its_transport(self):
        self.collected('appcycle')

    def test_left_for_the_interpreters_exit(self):
        self.child('exit')

    def test_held_by_a_traceback_until_exit(self):
        self.child('traceback')

    # Its destroy runs Python code during the interpreter's exit, the factory still pinned.
    def test_left_for_the_exit_with_a_log_factory_written_in_python(self):
        self.child('scriptexit')

    def test_held_by_a_traceback_with_a_log_factory_written_in_python(self):
        self.child('scripttraceback')

    # What the pins cost (#201): a factory that refers to its transport, directly or through its application, keeps
    # it, so the transport is never collected and its session stays registered. The process still exits cleanly.
    def kept(self, mode):
        lines, output = self.child(mode)
        self.assertEqual('1', lines.get('pinned'), output)
        self.assertEqual('1', lines.get('pins'), output)
        self.assertEqual('True', lines.get('registered'), output)

    def test_a_log_factory_that_refers_to_its_transport_keeps_it(self):
        self.kept('factorycycle')

    def test_a_log_factory_that_refers_to_its_application_keeps_the_transport(self):
        self.kept('appfactorycycle')

    # Kept so, started and logged on, and left running for the exit with its peer.
    def test_a_transport_so_kept_left_running_for_the_exit(self):
        lines, output = self.child('cycleexit')
        self.assertEqual('True', lines.get('loggedOn'), output)


if __name__ == '__main__':
    unittest.main()
