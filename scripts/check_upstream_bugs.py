#!/usr/bin/env python3
#
# Checks that every entry in Solidity's list of known bugs that can affect Hyperion has been
# reviewed, as SECURITY.md requires. Entries fixed in Solidity 0.8.23 or earlier, the version
# Hyperion was forked from, were fixed before the fork and need no review.
#
# docs/upstream_bugs_reviewed.json records each review: the date Solidity announced the bug and,
# once decided, the review date and either the Hyperion bug in docs/bugs.json ("hyperion") or why
# Hyperion is not affected ("notAffected"). The check fails when an entry is missing from the
# record, when a review is still open 30 days after the announcement, or when the record is
# inconsistent. The weekly "Upstream bugs" workflow runs it.

import argparse
import datetime
import json
import re
import sys
import urllib.request
from pathlib import Path

FORK_BASE = (0, 8, 23)
REVIEW_DAYS = 30
UPSTREAM_URL = 'https://raw.githubusercontent.com/argotorg/solidity/develop/docs/bugs.json'
RECORD = 'docs/upstream_bugs_reviewed.json'

root_path = Path(__file__).resolve().parent.parent


def version(version_string):
    return tuple(int(c) for c in version_string.split('.'))


def needs_review(bug):
    return 'fixed' not in bug or version(bug['fixed']) > FORK_BASE


def announcement_date(bug, today):
    # Solidity's blog posts carry the date in the path: https://blog.soliditylang.org/2026/10/22/...
    m = re.search(r'/(\d{4})/(\d{2})/(\d{2})/', bug.get('link', ''))
    try:
        return datetime.date(*map(int, m.groups()))
    except (AttributeError, ValueError):
        return today


def date_field(review, key, problems):
    try:
        return datetime.date.fromisoformat(review[key])
    except (KeyError, TypeError, ValueError):
        problems.append(f'{review.get("uid")}: "{key}" must be a date such as "2026-11-02"')
        return None


def check(upstream, record, hyperion_bugs, today):
    """Returns (problems, open_reviews). The check passes when problems is empty."""
    problems = []
    open_reviews = []
    upstream_by_uid = {bug['uid']: bug for bug in upstream}
    hyperion_by_uid = {bug['uid']: bug for bug in hyperion_bugs}
    reviewed_uids = set()

    for review in record:
        uid = review.get('uid')
        if uid in reviewed_uids:
            problems.append(f'{uid}: listed more than once in {RECORD}')
        reviewed_uids.add(uid)
        bug = upstream_by_uid.get(uid)
        if bug is None:
            problems.append(f"{uid}: not in Solidity's list of known bugs")
            continue
        if review.get('name') != bug['name']:
            problems.append(f'{uid}: {RECORD} names it {review.get("name")}, Solidity names it {bug["name"]}')
        announced = date_field(review, 'announced', problems)
        verdicts = [key for key in ('hyperion', 'notAffected') if key in review]
        if 'reviewed' not in review:
            if verdicts:
                problems.append(f'{uid}: has "{verdicts[0]}" but no "reviewed" date')
            elif announced is not None:
                due = announced + datetime.timedelta(days=REVIEW_DAYS)
                if today > due:
                    problems.append(f'{uid}: the review was due on {due}')
                else:
                    open_reviews.append(f'{uid}: review due by {due}')
            continue
        reviewed = date_field(review, 'reviewed', problems)
        if announced is not None and reviewed is not None and reviewed < announced:
            problems.append(f'{uid}: "reviewed" is earlier than "announced"')
        if len(verdicts) != 1:
            problems.append(f'{uid}: a reviewed entry needs exactly one of "hyperion" and "notAffected"')
        elif 'hyperion' in review:
            hyperion_bug = hyperion_by_uid.get(review['hyperion'])
            if hyperion_bug is None:
                problems.append(f'{uid}: {review["hyperion"]} is not in docs/bugs.json')
            elif uid not in json.dumps(hyperion_bug):
                problems.append(f'{uid}: the docs/bugs.json entry {review["hyperion"]} does not mention {uid}')
        elif not str(review['notAffected']).strip():
            problems.append(f'{uid}: "notAffected" must say why Hyperion is not affected')

    for bug in upstream:
        if needs_review(bug) and bug['uid'] not in reviewed_uids:
            announced = announcement_date(bug, today)
            problems.append(
                f'{bug["uid"]} ({bug["name"]}): not in {RECORD}. Add it with "announced": "{announced}" '
                f'and decide by {announced + datetime.timedelta(days=REVIEW_DAYS)}.'
            )
    return problems, open_reviews


def main():
    parser = argparse.ArgumentParser(description="Check that Solidity's known bugs are reviewed for Hyperion.")
    parser.add_argument('--upstream', type=Path, help="read Solidity's bugs.json from this file instead of downloading it")
    parser.add_argument('--today', type=datetime.date.fromisoformat, default=datetime.date.today())
    args = parser.parse_args()

    if args.upstream:
        upstream = json.loads(args.upstream.read_text(encoding='utf8'))
    else:
        with urllib.request.urlopen(UPSTREAM_URL, timeout=60) as response:
            upstream = json.load(response)
    record = json.loads((root_path / RECORD).read_text(encoding='utf8'))
    hyperion_bugs = json.loads((root_path / 'docs/bugs.json').read_text(encoding='utf8'))

    problems, open_reviews = check(upstream, record, hyperion_bugs, args.today)
    for line in open_reviews:
        print(line)
    if problems:
        sys.exit('\n'.join(problems))
    count = sum(needs_review(bug) for bug in upstream)
    print(f"All {count} entries in Solidity's list of known bugs that were fixed after 0.8.23, or not yet, are in {RECORD}.")


if __name__ == '__main__':
    main()
