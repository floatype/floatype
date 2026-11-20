"""Tests for mixed_english.py's helpers and its sets. Run from this directory: python3 -m unittest mixed_english_test"""

import re
import unittest

import mixed_english


class MixedEnglishTest(unittest.TestCase):

  def test_load_items_splits_fields_and_answers(self):
    text = ("# a comment\n\n"
            "mid-sentence\t今日は\ttestwokaku.\ttestを書く。 | testをかく。\n"
            "slash\t\t2026/11/20\t2026/11/20\n")
    self.assertEqual(mixed_english.load_items(text, "dev"), [
        {"set": "dev", "category": "mid-sentence", "context": "今日は",
         "keys": "testwokaku.", "expected": ["testを書く。", "testをかく。"]},
        {"set": "dev", "category": "slash", "context": "",
         "keys": "2026/11/20", "expected": ["2026/11/20"]},
    ])

  def test_load_items_rejects_other_lines(self):
    for line in ("slash\t\ta/b",                    # three fields
                 "english\t\ttest\ttest",           # not a category
                 "english-loose\t\tTest\ttest",     # upper case has no key
                 "mid-sentence\t\tkyou ha\t今日は",  # Space converts
                 "english-loose\t\ttest\t"):        # no answer
      with self.assertRaises(SystemExit, msg=line):
        mixed_english.load_items(line + "\n", "dev")

  def test_score_takes_any_answer_and_absorbs_width_only_in_nfkc(self):
    item = {"category": "mid-sentence", "expected": ["3時からtest", "三時からtest"]}
    row = mixed_english.score(item, "三時からtest\tさんじからtest\t2\t1")
    self.assertTrue(row["exact"])
    row = mixed_english.score(item, "３時からｔｅｓｔ\tさんじからtest\t2\t1")
    self.assertFalse(row["exact"])
    self.assertTrue(row["nfkc"])
    self.assertFalse(row["false_positive"])  # English belongs here.
    self.assertFalse(row["timeout"])

  def test_score_counts_letters_in_japanese_as_false_positives(self):
    item = {"category": "japanese-sentence", "expected": ["宿題をして"]}
    for got in ("宿題をsite", "宿題をｓｉｔｅ"):
      row = mixed_english.score(item, f"{got}\tしゅくだいをして\t0\t0")
      self.assertFalse(row["nfkc"])
      self.assertTrue(row["false_positive"])
    row = mixed_english.score(item, "宿題を仕手\tしゅくだいをして\t0\t0")
    self.assertFalse(row["nfkc"])
    self.assertFalse(row["false_positive"])

  def test_score_misses_a_skipped_item_and_marks_a_timeout(self):
    item = {"category": "english-loose", "expected": ["test"]}
    row = mixed_english.score(item, "SKIP")
    self.assertTrue(row["skipped"])
    self.assertFalse(row["nfkc"])
    self.assertFalse(row["timeout"])
    self.assertTrue(mixed_english.score(item, "test\ttest\t29\t0")["timeout"])

  def test_the_typing_sets_are_well_formed(self):
    items = (mixed_english.read_set("typing_dev")
             + mixed_english.read_set("typing_heldout"))
    typed = [(i["context"], i["keys"]) for i in items]
    self.assertEqual(len(typed), len(set(typed)))
    for item in items:
      if item["category"] == "japanese-space":
        # Japanese, then one Space to convert it.
        self.assertRegex(item["keys"], r"^[a-z]+ $", item)
        for answer in item["expected"]:
          self.assertIsNone(re.search("[A-Za-z ]", answer), item)
      elif item["category"] in ("run-together", "english-sentence", "token"):
        self.assertEqual(item["expected"], [item["keys"]], item)

  def test_the_sets_are_well_formed(self):
    items = mixed_english.read_set("dev") + mixed_english.read_set("heldout")
    typed = [(i["context"], i["keys"]) for i in items]
    self.assertEqual(len(typed), len(set(typed)))
    for item in items:
      for answer in item["expected"]:
        if item["category"] in mixed_english.JAPANESE:
          self.assertIsNone(re.search("[a-z]", answer), item)
        else:
          # The English and the digits of an answer are typed as they are.
          typed_as_is = re.findall("[a-z0-9]+", answer)
          self.assertIsNotNone(
              re.search(".*".join(map(re.escape, typed_as_is)), item["keys"]),
              item)
        self.assertIsNone(re.search("[A-Z ]", answer), item)


if __name__ == "__main__":
  unittest.main()
