"""Test runner for the parser package.

Usage (from the workspace root or anywhere):

    python parser/tests/run_tests.py

Writes the detailed report to ``build/parser_test_out.txt`` (if the build
directory exists / can be created) and a one-line summary to stdout.
"""

import os
import sys
import unittest

# Make the workspace root importable so ``from parser import ...`` works
# regardless of the current working directory.
_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)


def main() -> int:
    loader = unittest.TestLoader()
    start = os.path.dirname(os.path.abspath(__file__))
    suite = loader.discover(start)

    report_path = os.path.join(_ROOT, "build", "parser_test_out.txt")
    try:
        os.makedirs(os.path.dirname(report_path), exist_ok=True)
        report_file = open(report_path, "w", encoding="utf-8")
    except OSError:
        report_file = None

    try:
        verbose_stream = report_file or sys.stderr
        runner = unittest.TextTestRunner(stream=verbose_stream, verbosity=2)
        result = runner.run(suite)
        summary = "Tests: %d run, %d failures, %d errors, %d skipped" % (
            result.testsRun,
            len(result.failures),
            len(result.errors),
            len(result.skipped),
        )
        print(summary)
        print("RESULT: %s" % ("OK" if result.wasSuccessful() else "FAILED"))
        if report_file:
            print("Detailed report: %s" % report_path)
        return 0 if result.wasSuccessful() else 1
    finally:
        if report_file:
            report_file.close()


if __name__ == "__main__":
    sys.exit(main())
