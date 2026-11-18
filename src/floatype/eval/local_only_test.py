"""Tests for local_only.py's symbol filter. Run: python3 -m unittest floatype/eval/local_only_test.py"""

import unittest

import local_only


class LocalOnlyTest(unittest.TestCase):

  def test_finds_network_symbols_and_libraries(self):
    self.assertEqual(
        local_only.network_uses(
            "tool",
            ["_malloc", "_getaddrinfo", "_socketpair", "_connect",
             "_CFSocketCreateWithNative"],
            ["/usr/lib/libSystem.B.dylib",
             "/System/Library/Frameworks/CFNetwork.framework/CFNetwork"]),
        ["_getaddrinfo", "_connect", "_CFSocketCreateWithNative",
         "/System/Library/Frameworks/CFNetwork.framework/CFNetwork"])

  def test_socketpair_is_local(self):
    self.assertEqual(local_only.network_uses("conv", ["_socketpair"], []), [])

  def test_allows_qts_event_loop(self):
    self.assertEqual(
        local_only.network_uses("QtCore", ["_CFSocketCreateWithNative"], []),
        [])


if __name__ == "__main__":
  unittest.main()
