"""Tests for the AJIMEE-Bench scoring helpers. Run: python3 -m unittest floatype/eval/ajimee_test.py"""

import unittest

import ajimee


class AjimeeTest(unittest.TestCase):

  def test_katakana_to_hiragana_keeps_other_characters(self):
    self.assertEqual(
        ajimee.katakana_to_hiragana("セイネンシ、ヴァーチャル（ABC）"),
        "せいねんし、ゔぁーちゃる（ABC）",
    )

  def test_levenshtein(self):
    self.assertEqual(ajimee.levenshtein("", ""), 0)
    self.assertEqual(ajimee.levenshtein("今日は", "今日歯"), 1)
    self.assertEqual(ajimee.levenshtein("abc", ""), 3)
    self.assertEqual(ajimee.levenshtein("kitten", "sitting"), 3)

  def test_score_uses_any_accepted_answer(self):
    item = {"index": "1", "input": "カク", "expected_output": ["描く", "書く"]}
    row = ajimee.score(item, "書く")
    self.assertTrue(row["exact"])
    self.assertEqual(row["min_cer"], 0.0)
    self.assertFalse(row["has_context"])

  def test_nfkc_absorbs_width_differences_only_in_nfkc_metric(self):
    item = {"index": "2", "input": "エーアイ", "expected_output": ["ＡＩ"], "context_text": "x"}
    row = ajimee.score(item, "AI")
    self.assertFalse(row["exact"])
    self.assertTrue(row["nfkc"])
    self.assertTrue(row["has_context"])

  def test_summarize_splits_by_context(self):
    rows = [
        {"has_context": True, "exact": True, "nfkc": True, "min_cer": 0.0},
        {"has_context": False, "exact": False, "nfkc": True, "min_cer": 0.5},
    ]
    summary = ajimee.summarize(rows)
    self.assertEqual(summary["all"]["acc1"], 0.5)
    self.assertEqual(summary["all"]["acc1_nfkc"], 1.0)
    self.assertEqual(summary["with_context"]["n"], 1)
    self.assertEqual(summary["without_context"]["min_cer"], 0.5)


if __name__ == "__main__":
  unittest.main()
