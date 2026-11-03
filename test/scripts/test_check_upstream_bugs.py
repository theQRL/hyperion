"""Exercise the review check for Solidity's list of known bugs."""
import datetime
import json
from pathlib import Path
import unittest

from check_upstream_bugs import check, RECORD

ROOT = Path(__file__).resolve().parents[2]
TODAY = datetime.date(2026, 11, 2)

UPSTREAM = [
    {'uid': 'SOL-2026-9', 'name': 'AffectsHyperion', 'introduced': '0.7.0', 'fixed': '0.8.40'},
    {'uid': 'SOL-2026-8', 'name': 'NewerFeature', 'introduced': '0.8.30', 'fixed': '0.8.40'},
    {'uid': 'SOL-2026-10', 'name': 'NotFixedYet', 'introduced': '0.8.0'},
    {'uid': 'SOL-2023-1', 'name': 'FixedBeforeTheFork', 'introduced': '0.8.13', 'fixed': '0.8.17'},
]
HYPERION_BUGS = [{'uid': 'HYP-2026-1', 'name': 'AffectsHyperion', 'description': 'Same as upstream SOL-2026-9.'}]


def reviewed_record():
    return [
        {'uid': 'SOL-2026-9', 'name': 'AffectsHyperion', 'announced': '2026-10-01', 'reviewed': '2026-10-05',
         'hyperion': 'HYP-2026-1'},
        {'uid': 'SOL-2026-8', 'name': 'NewerFeature', 'announced': '2026-10-01', 'reviewed': '2026-10-02',
         'notAffected': 'The feature arrived after the fork.'},
        {'uid': 'SOL-2026-10', 'name': 'NotFixedYet', 'announced': '2026-10-20'},
    ]


class TestCheckUpstreamBugs(unittest.TestCase):
    def problems(self, record, today=TODAY, hyperion_bugs=None):
        return check(UPSTREAM, record, HYPERION_BUGS if hyperion_bugs is None else hyperion_bugs, today)[0]

    def test_complete_record_passes_and_reports_open_reviews(self):
        problems, open_reviews = check(UPSTREAM, reviewed_record(), HYPERION_BUGS, TODAY)
        self.assertEqual(problems, [])
        self.assertEqual(open_reviews, ['SOL-2026-10: review due by 2026-11-19'])

    def test_missing_entry_fails(self):
        record = [review for review in reviewed_record() if review['uid'] != 'SOL-2026-8']
        problems = self.problems(record)
        self.assertEqual(len(problems), 1)
        self.assertIn('SOL-2026-8 (NewerFeature): not in ' + RECORD, problems[0])
        self.assertIn('"announced": "2026-11-02" and decide by 2026-12-02', problems[0])

    def test_missing_entry_suggests_the_announcement_date_from_its_link(self):
        upstream = UPSTREAM + [{'uid': 'SOL-2026-11', 'name': 'Announced', 'fixed': '0.8.41',
                                'link': 'https://blog.soliditylang.org/2026/10/30/announced-bug/'}]
        problems = check(upstream, reviewed_record(), HYPERION_BUGS, TODAY)[0]
        self.assertEqual(problems, [
            f'SOL-2026-11 (Announced): not in {RECORD}. Add it with "announced": "2026-10-30" and decide by 2026-11-29.'
        ])

    def test_entry_fixed_before_the_fork_needs_no_review(self):
        self.assertNotIn('SOL-2023-1', ' '.join(self.problems(reviewed_record())))

    def test_open_review_fails_after_30_days(self):
        self.assertEqual(self.problems(reviewed_record(), today=datetime.date(2026, 11, 19)), [])
        self.assertEqual(
            self.problems(reviewed_record(), today=datetime.date(2026, 11, 20)),
            ['SOL-2026-10: the review was due on 2026-11-19'],
        )

    def test_reviewed_entry_needs_exactly_one_verdict(self):
        for verdicts in ({}, {'hyperion': 'HYP-2026-1', 'notAffected': 'Both.'}):
            record = reviewed_record()
            del record[0]['hyperion']
            record[0].update(verdicts)
            self.assertEqual(
                self.problems(record),
                ['SOL-2026-9: a reviewed entry needs exactly one of "hyperion" and "notAffected"'],
            )

    def test_verdict_without_review_date_fails(self):
        for key in ('notAffected', 'late'):
            record = reviewed_record()
            record[2][key] = 'Decided but not dated.'
            self.assertEqual(self.problems(record), [f'SOL-2026-10: has "{key}" but no "reviewed" date'])

    def test_verdicts_must_be_text(self):
        for value in (None, False, '  '):
            record = reviewed_record()
            record[1]['notAffected'] = value
            self.assertEqual(self.problems(record), ['SOL-2026-8: "notAffected" must say why Hyperion is not affected'])
        record = reviewed_record()
        record[0]['hyperion'] = 1
        self.assertEqual(self.problems(record), ['SOL-2026-9: 1 is not an entry in docs/bugs.json'])

    def test_announced_must_match_the_date_of_the_solidity_announcement(self):
        upstream = [dict(bug) for bug in UPSTREAM]
        upstream[2]['link'] = 'https://blog.soliditylang.org/2026/10/10/not-fixed-yet-bug/'
        problems, open_reviews = check(upstream, reviewed_record(), HYPERION_BUGS, TODAY)
        self.assertEqual(problems, ['SOL-2026-10: "announced" is 2026-10-20, but Solidity announced it on 2026-10-10'])
        # The deadline counts from the real announcement, not the recorded date.
        self.assertEqual(open_reviews, ['SOL-2026-10: review due by 2026-11-09'])

    def test_late_decision_must_say_why(self):
        record = reviewed_record()
        record[0]['reviewed'] = '2026-11-01'
        self.assertEqual(self.problems(record), [
            'SOL-2026-9: reviewed on 2026-11-01, after the 2026-10-31 deadline, so "late" must say why'
        ])
        record[0]['late'] = ''
        self.assertEqual(len(self.problems(record)), 1)
        record[0]['late'] = 'Announced before the 30-day review was adopted.'
        self.assertEqual(self.problems(record), [])
        record[0]['reviewed'] = '2026-10-31'
        self.assertEqual(self.problems(record), ['SOL-2026-9: has "late" but was reviewed by the 2026-10-31 deadline'])

    def test_hyperion_entry_must_exist_and_mention_the_solidity_bug(self):
        self.assertEqual(
            self.problems(reviewed_record(), hyperion_bugs=[]),
            ["SOL-2026-9: 'HYP-2026-1' is not an entry in docs/bugs.json"],
        )
        self.assertEqual(
            self.problems(reviewed_record(), hyperion_bugs=[{'uid': 'HYP-2026-1', 'name': 'AffectsHyperion'}]),
            ['SOL-2026-9: the docs/bugs.json entry HYP-2026-1 does not mention SOL-2026-9'],
        )

    def test_record_must_match_upstream(self):
        record = reviewed_record()
        record[1]['name'] = 'Renamed'
        record.append({'uid': 'SOL-2099-1', 'name': 'Typo', 'announced': '2026-10-01'})
        record.append(dict(record[0]))
        self.assertEqual(self.problems(record), [
            f'SOL-2026-8: {RECORD} names it Renamed, Solidity names it NewerFeature',
            "SOL-2099-1: not in Solidity's list of known bugs",
            f'SOL-2026-9: listed more than once in {RECORD}',
        ])

    def test_dates_must_be_valid_and_ordered(self):
        record = reviewed_record()
        record[0]['announced'] = '1 October'
        record[1]['reviewed'] = '2026-09-30'
        self.assertEqual(self.problems(record), [
            'SOL-2026-9: "announced" must be a date such as "2026-11-02"',
            'SOL-2026-8: "reviewed" is earlier than "announced"',
        ])

    def test_committed_record_is_consistent(self):
        # Without network access, check the committed record against an upstream list built from it.
        record = json.loads((ROOT / RECORD).read_text(encoding='utf8'))
        hyperion_bugs = json.loads((ROOT / 'docs/bugs.json').read_text(encoding='utf8'))
        upstream = [{'uid': review['uid'], 'name': review['name'], 'fixed': '0.8.99'} for review in record]
        today = max(datetime.date.fromisoformat(review['reviewed']) for review in record)
        problems, open_reviews = check(upstream, record, hyperion_bugs, today)
        self.assertEqual(problems, [])
        self.assertEqual(open_reviews, [])


if __name__ == '__main__':
    unittest.main()
