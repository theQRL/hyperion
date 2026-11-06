# Security Policy

The Hyperion team and community take all security bugs in Hyperion seriously.
We appreciate your efforts and responsible disclosure and will make every effort to acknowledge your contributions.

## Scope

Bugs in the Hyperion repository are in scope.
Bugs in third-party dependencies e.g., jsoncpp, boost etc. are not in scope unless they result in a Hyperion specific bug.

Only bugs that have a demonstrable security impact on smart contracts are in scope.
For example, a Hyperion program whose optimization is incorrect (e.g., leads to an incorrect output) qualifies as a security bug.
Please note that the repository's GitHub security advisory process has precedence over this security policy.

## Supported Versions

As a general rule, only the latest release gets security updates.
Exceptions may be made when the current breaking release is relatively new, e.g. less than three months old.
If you are reporting a bug, please state clearly the Hyperion version(s) it affects.

Example 1: Assuming the current release is `0.6.3` and a security bug has been found in it that affects both `0.5.x` and `0.6.x` trees, we may not only patch `0.6.3` (the bug-fix release numbered `0.6.4`) but `0.5.x` as well (the bug-fix release numbered `0.5.(x+1)`).

Example 2: Assuming the current release is `0.6.25` and a security bug has been found in it, we may only patch `0.6.25` (in the bug-fix release numbered `0.6.26`) even if the bug affects a previous tree such as `0.5.x`.

## Reporting a Vulnerability

To report a vulnerability, please open a private security advisory for this repository.

In the bug report, please include all details necessary to reproduce the vulnerability such as:

- Input program that triggers the bug
- Compiler version affected
- Target QRVM version
- Framework/IDE if applicable
- QRVM execution environment/client if applicable
- Operating system

Please include steps to reproduce the bug you have found in as much detail as possible.

Once we have received your bug report, we will try to reproduce it and provide a more detailed response.
Once the reported bug has been successfully reproduced, the Hyperion team will work on a fix.

The Hyperion team maintains the following JSON-formatted lists of known security vulnerabilities, including those no release has fixed yet:

- [Summary of known security vulnerabilities][3]
- [List of security vulnerabilities affecting a specific version of the compiler][4].

Hyperion is a fork of Solidity 0.8.23, so security bugs found in Solidity can affect it too.
Within 30 days of Solidity announcing a new entry in [its list of known bugs][5], the Hyperion team decides whether the bug affects Hyperion.
If it does, the bug gets an entry in `docs/bugs.json` within those 30 days, whether or not a fix is ready, and the changelog entry of the release that fixes it names the Solidity identifier, for example `(upstream SOL-2026-5)`.
Bugs fixed in Solidity 0.8.23 or earlier need no decision, because Hyperion was forked with those fixes.
`docs/upstream_bugs_reviewed.json` records every decision, including each bug found not to affect Hyperion and, for a decision made after the 30 days, why it was late.
A weekly CI job fails when any other entry in Solidity's list is missing from that record, or when its decision is still open 30 days after the announcement.

The [release checklist][6] describes how a release is made and published, how each release updates both lists, and how the review of Solidity's bugs is checked before every release.


[3]: docs/bugs.rst
[4]: https://github.com/theQRL/hyperion/blob/main/docs/bugs_by_version.json
[5]: https://github.com/argotorg/solidity/blob/develop/docs/bugs.json
[6]: ReleaseChecklist.md
