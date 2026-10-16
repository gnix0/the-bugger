"""M00 executable contract; NFR-01/03/08/12 foundation evidence."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import termios
import unittest


BINARY = Path(sys.argv.pop(1)).resolve()


class CliTests(unittest.TestCase):
    def run_cli(self, *args, **kwargs):
        return subprocess.run(
            [str(BINARY), *args],
            stdin=subprocess.DEVNULL,
            capture_output=True,
            timeout=5,
            check=False,
            **kwargs,
        )

    def test_version(self):
        result = self.run_cli("--version")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"the-bugger 0.1.0\n")
        self.assertEqual(result.stderr, b"")

    def test_help(self):
        result = self.run_cli("--help")
        self.assertEqual(result.returncode, 0)
        for text in (b"Usage:", b"--help", b"--version", b"not implemented"):
            self.assertIn(text, result.stdout)
        self.assertEqual(result.stderr, b"")

    def test_bare_invocation_reports_current_status(self):
        result = self.run_cli()
        self.assertEqual(result.returncode, 0)
        self.assertIn(b"M00 foundation", result.stdout)
        self.assertIn(b"not implemented", result.stdout)
        self.assertEqual(result.stderr, b"")

    def test_invalid_arguments_are_rejected(self):
        for args in (("--unknown",), ("file.elf",), ("",),
                     ("--help", "extra"), ("--version", "--help")):
            with self.subTest(args=args):
                result = self.run_cli(*args)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, b"")
                self.assertEqual(
                    result.stderr,
                    b"the-bugger: expected no arguments, --help, or --version.\n"
                    b"Run the-bugger --help for usage.\n",
                )

    def test_arguments_are_not_executed_or_echoed_as_terminal_controls(self):
        with tempfile.TemporaryDirectory(prefix="the-bugger-cli-") as directory:
            marker = Path(directory) / "unexpected"
            argument = f"$(touch '{marker}')\x1b[2J"
            result = self.run_cli(argument)
            self.assertEqual(result.returncode, 1)
            self.assertFalse(marker.exists())
            self.assertNotIn(b"\x1b", result.stdout + result.stderr)

    def test_runs_without_terminal_environment_or_project_working_directory(self):
        with tempfile.TemporaryDirectory(prefix="the-bugger-cli-") as directory:
            environment = os.environ.copy()
            environment.pop("TERM", None)
            result = self.run_cli("--version", cwd=directory, env=environment)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b"the-bugger 0.1.0\n")
            self.assertEqual(result.stderr, b"")
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_tty_attributes_remain_unchanged(self):
        master, slave = os.openpty()
        try:
            before = termios.tcgetattr(slave)
            for args in ((), ("--help",), ("--version",), ("--unknown",)):
                with self.subTest(args=args):
                    result = subprocess.run(
                        [str(BINARY), *args], stdin=slave, stdout=slave,
                        stderr=slave, timeout=5, check=False,
                    )
                    self.assertEqual(result.returncode, 1 if args == ("--unknown",) else 0)
                    self.assertEqual(termios.tcgetattr(slave), before)
        finally:
            os.close(slave)
            os.close(master)


if __name__ == "__main__":
    unittest.main(verbosity=2)
