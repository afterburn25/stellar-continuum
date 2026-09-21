"""Run a unittest file while skipping explicitly named baseline failures.

unittest's ``-k`` option only supports fnmatch inclusion patterns; there is no
negation.  CI needs to skip a short list of documented baseline failures (see
docs/KNOWN_ISSUES.md) without hiding any other test, so this runner loads the
test module, walks the suite, and drops only the named test IDs.

Usage:
    python filtered_test_runner.py <test-file> [TestClass.test_method ...]

Each excluded ID is matched against the tail of the test's dotted id() so the
module prefix is not required.  Exclusions that match nothing are reported but
do not fail the run on their own; all remaining tests still execute normally.
"""
import importlib.util
import sys
import unittest
from pathlib import Path


def _collect(suite, kept, excluded, matched):
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            _collect(test, kept, excluded, matched)
            continue
        test_id = test.id()
        if any(test_id == e or test_id.endswith("." + e) for e in excluded):
            matched.update(e for e in excluded if test_id == e or test_id.endswith("." + e))
            continue
        kept.addTest(test)


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: filtered_test_runner.py <test-file> [excluded-id ...]")
    path = Path(sys.argv[1]).resolve()
    excluded = {e for e in sys.argv[2:] if e}
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[path.stem] = module
    spec.loader.exec_module(module)
    suite = unittest.defaultTestLoader.loadTestsFromModule(module)
    kept = unittest.TestSuite()
    matched = set()
    _collect(suite, kept, excluded, matched)
    for missing in sorted(excluded - matched):
        print(f"filtered_test_runner: exclusion did not match any test: {missing}", file=sys.stderr)
    result = unittest.TextTestRunner(verbosity=2).run(kept)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
