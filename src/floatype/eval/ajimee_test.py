"""Tests for the AJIMEE-Bench scoring helpers. Run from this directory: python3 -m unittest ajimee_test"""

import unittest

import ajimee


class AjimeeTest(unittest.TestCase):

  def test_katakana_to_hiragana_keeps_other_characters(self):
    self.assertEqual(
        ajimee.katakana_to_hiragana("セイネンシ、ヴァーチャル（ABC）"),
        "せいねんし、ゔぁーちゃる（ABC）",
    )

  def test_hiragana_to_katakana_round_trips(self):
    reading = "ゔぁーじょん、wikipediaの０ばん"
    self.assertEqual(ajimee.hiragana_to_katakana(reading), "ヴァージョン、wikipediaノ０バン")
    self.assertEqual(ajimee.katakana_to_hiragana(ajimee.hiragana_to_katakana(reading)), reading)

  def test_parse_anthy_corpus_joins_differently_segmented_sides(self):
    text = (
        "#\n# copying: public domain\n"
        "|おんせんで|つめを|といで|きました| |温泉で|爪を|研いできました|\n"
        "|。|０なんて| |.|0なんて|\n"
    )
    self.assertEqual(ajimee.parse_anthy_corpus(text), [
        {"index": "3", "context_text": "", "input": "オンセンデツメヲトイデキマシタ",
         "expected_output": ["温泉で爪を研いできました"]},
        {"index": "4", "context_text": "", "input": "。０ナンテ", "expected_output": [".0なんて"]},
    ])

  def test_parse_anthy_corpus_rejects_other_lines(self):
    with self.assertRaises(SystemExit):
      ajimee.parse_anthy_corpus("|よみ|\n")

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

  def test_zenz_request_line_matches_the_scorer_protocol(self):
    # Same expectation as FormatRequest in floatype/scorer/protocol_test.cc.
    self.assertEqual(
        ajimee.zenz_request_line(42, "熱が\tある\r\nよ", "キョウ"), "42\t熱が あるよ\tキョウ\n"
    )
    self.assertEqual(ajimee.zenz_request_line(0, "", "キョウ"), "0\t\tキョウ\n")

  def test_committed_after_takes_zenz_only_when_it_came_in_time(self):
    row = {"mozc": "機会", "final": "機械", "final_ms": 25.0}
    self.assertEqual(ajimee.committed_after(row, 20), "機会")
    self.assertEqual(ajimee.committed_after(row, 25), "機械")
    self.assertEqual(ajimee.committed_after(row, 100), "機械")

  def test_azookey_top1_takes_the_first_output_or_empty(self):
    result = {"items": [
        {"query": "かんじ", "outputs": [{"text": "漢字", "score": -2.0}, {"text": "感じ", "score": -1.0}]},
        {"query": "０なんて", "outputs": []},
    ]}
    self.assertEqual(ajimee.azookey_top1(result, ["かんじ", "０なんて"]), ["漢字", ""])
    with self.assertRaises(SystemExit):
      ajimee.azookey_top1(result, ["かんじ"])

  def test_unseen_sample_leaves_out_seen_readings_and_repeats(self):
    items = [{"input": r} for r in ["ア", "イ", "ア", "ウ", "エ", "オ", "カ"]]
    sample = ajimee.unseen_sample(items, {"イ"}, 2)
    # ア ウ エ オ カ are fresh; every second of them, two in all.
    self.assertEqual([item["input"] for item in sample], ["ア", "エ"])

  def test_parse_anthy_corpus_can_skip_lines_in_another_form(self):
    text = "|あめ| |雨|\nあめ |あめ| |雨|\n"
    self.assertEqual(len(ajimee.parse_anthy_corpus(text, skip_other_lines=True)), 1)
    with self.assertRaises(SystemExit):
      ajimee.parse_anthy_corpus(text)

  def test_latency_summary(self):
    summary = ajimee.latency_summary([float(i) for i in range(1, 101)])
    self.assertEqual(summary["p50_ms"], 51.0)
    self.assertEqual(summary["p99_ms"], 99.0)
    self.assertEqual(summary["max_ms"], 100.0)

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
