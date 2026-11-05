## Checklist for making a release:

A release has three parts: the hyperion tag, whose CI run builds and publishes the binaries; a pull request
in hypc-bin, which publishes them on `binaries.theqrl.org`; and the hypc-js release, which publishes
[``@theqrl/hypc``](https://www.npmjs.com/package/@theqrl/hypc) to npm. Hyperion has no Docker image, Linux
distribution package, PPA, snap or Homebrew formula. The unscoped npm package ``hypc`` belongs to someone else.

### Requirements
 - [ ] GitHub account with access to [hyperion](https://github.com/theQRL/hyperion), [hypc-js](https://github.com/theQRL/hypc-js)
       and [hypc-bin](https://github.com/theQRL/hypc-bin).
 - [ ] [npm Registry](https://www.npmjs.com) account with two-factor authentication, added as a maintainer of [``@theqrl/hypc``](https://www.npmjs.com/package/@theqrl/hypc).
       Publishing itself runs only from CI through npm trusted publishing.
 - [ ] A GPG or SSH signing key added to your GitHub account as a signing key, for signing release tags.
 - [ ] A second maintainer who can approve the ``release`` (hyperion) and ``npm-publish`` (hypc-js) environments.
       Nobody can approve their own release.

### Pre-flight checks
At least a day before the release:
 - [ ] Run ``make linkcheck`` from within ``docs/`` and fix any broken links it finds.
       Ignore false positives caused by ``href`` anchors and dummy links not meant to work.
 - [ ] Build the documentation with ``docs/docs.sh`` and fix any warnings.
 - [ ] Make sure that all merged PRs that should have changelog entries do have them.
 - [ ] Make sure that the weekly ``Upstream bugs`` workflow ran in the past week and passed, or run ``scripts/check_upstream_bugs.py``.
       It confirms that every entry in [Solidity's list of known bugs](https://github.com/argotorg/solidity/blob/develop/docs/bugs.json)
       not fixed in Solidity 0.8.23 or earlier is recorded in ``docs/upstream_bugs_reviewed.json`` and decided within the 30 days
       that ``SECURITY.md`` allows, and that each one that affects Hyperion has an entry in ``docs/bugs.json``. Changelog entries
       for fixes of these bugs name the Solidity identifier, whether the fix was ported from Solidity or written for Hyperion,
       for example ``(upstream SOL-2026-5)``.
 - [ ] Rerun CI on the top commits of main branches in all repositories that do not have daily activity by creating a test branch or PR:
      - [ ] ``hypc-js``
      - [ ] ``hypc-bin`` (make sure the bytecode comparison check did run)
 - [ ] Check that the ``deps/hypc-js`` submodule records the latest commit on ``hypc-js`` ``main``, and merge or create the pull request that moves it if not.
       The ``hypc-js`` tests in CI, which gate the release, run at that commit.
 - [ ] Verify that the release tarball of ``hypc-js`` works.
       Bump version locally, add ``hypjson.js`` from CI, build it, compare the file structure with the previous version, install it locally and try to use it.

### Drafts
At least a day before the release:
 - [ ] Create a draft PR to sort the changelog.
 - [ ] Create draft PRs to bump version in ``hyperion`` and ``hypc-js``.
 - [ ] Prepare the release notes. Do not create a draft release on GitHub: publishing a draft whose tag
       does not exist yet creates an unsigned lightweight tag, which CI rejects and which burns the version number.
 - [ ] Prepare the announcement.

### Changelog
 - [ ] Sort the changelog entries alphabetically and correct any errors you notice. Commit it.
 - [ ] Rename ``### Unreleased`` to ``### <version> (<YYYY-MM-DD>)``, the heading form ``scripts/update_bugs_by_version.py`` reads, and start a new ``### Unreleased`` section above it.
 - [ ] In ``docs/bugs.json``, set ``fixed`` to the new version on every bug this release fixes, and add an entry for any bug it fixes that is not listed yet.
 - [ ] Run ``scripts/update_bugs_by_version.py`` to regenerate ``bugs_by_version.json`` from the changelog and ``bugs.json``.
       Make sure that the entry for the new version lists exactly the known bugs the release still contains.
 - [ ] Commit changes, create a pull request and wait for the tests. Then merge it.
 - [ ] Copy the changelog into the release notes.

### Create the Release
Release tags are immutable. Tag rulesets block moving or deleting ``v*`` tags, and only admins can create them.
CI publishes only from a signed, annotated tag on a commit of ``main`` (``scripts/ci/verify_release_tag.sh``).

**A bad tag burns the number.** Never delete, move or re-create a release tag or a published release.
If a tag or release is wrong, fix it on ``main`` and release the next patch version.

 - [ ] Check that all PRs for the release, including changelog cleanup and date, are merged,
       and that all tests on the latest commit in ``main`` are green.
 - [ ] Create a signed, annotated tag on that commit from the command line and push only that tag:
       ``git fetch origin && git tag -s v$VERSION -m "Hyperion v$VERSION" origin/main && git push origin v$VERSION``.
       Do not create the tag from the GitHub release page: it creates an unsigned lightweight tag, which CI rejects.
 - [ ] Wait for the CI run on the tag. Its ``Verify release tag`` job must pass.
       After every build has passed, the ``Publish GitHub Release`` job waits for approval of the ``release`` environment.
       Ask another maintainer to review the run and approve it; you cannot approve your own release.
 - [ ] Once CI has published the release, edit its notes on GitHub.
       Include the following warning: ``**The release is still in progress and the binaries may not yet be available from all sources.**``.
 - [ ] Thank voluntary contributors in the GitHub release notes.
       Use ``scripts/list_contributors.sh v<previous version>`` to get initial list of names.
       Remove different variants of the same name manually before using the output.

### Upload Release Artifacts and Publish Binaries
 - [ ] Do not upload files to the release by hand: a file built on a workstation has no build provenance and is not
       covered by ``SHA256SUMS``. GitHub attaches source archives generated from the signed tag. If a separate source
       tarball is needed again, build it from the tag and attest it in the release job, like the binaries.
 - [ ] Wait for GitHub Actions to publish the tagged build's release assets after all checks pass and the ``release`` environment is approved.
       Verify that the release contains ``hypc-windows.exe``, ``hypc-macos``, ``hypc-static-linux`` (AMD64),
       ``hypc-static-linux-arm`` (ARM64), ``hypjson.js`` and ``SHA256SUMS``, and that ``file hypc-macos`` reports a
       Mach-O universal binary with ``x86_64`` and ``arm64`` slices.
 - [ ] Update hypc-bin separately using the binaries from the same tagged GitHub Release:
       run ``node scripts/add-release.mjs $VERSION`` in ``hypc-bin``. It downloads the assets, checks them against the
       release ``SHA256SUMS`` and the build provenance attestation, and regenerates ``bin/list.json``.
 - [ ] Create a pull request in hypc-bin, wait for its checks and a review, and merge.

### Release hypc-js
Follow [``RELEASE.md`` in hypc-js](https://github.com/theQRL/hypc-js/blob/main/RELEASE.md). In short:
 - [ ] Wait until hypc-bin is deployed: [``list.json``](https://binaries.theqrl.org/bin/list.json) lists the new version
       and its ``hypjson.js`` downloads.
 - [ ] Open a pull request that sets ``"version"`` in ``package.json`` and runs ``npm run updateBinary``.
       Merge it after its checks pass and it has been reviewed.
 - [ ] Tag the merge commit with a signed tag and push only that tag: ``git tag -s v$VERSION -m v$VERSION && git push origin v$VERSION``.
 - [ ] Ask another maintainer to approve the ``npm-publish`` environment.
       The ``Release`` workflow then publishes the tarball to npm with provenance and creates the GitHub release.
 - [ ] Never run ``npm publish`` or ``npm run publish:tarball`` by hand.

### Post-release
 - [ ] Remove "still in progress" warning from the [release notes](https://github.com/theQRL/hyperion/releases).
 - [ ] Create a pull request on ``main`` that increases ``PROJECT_VERSION`` in ``CMakeLists.txt``.
 - [ ] Announce the release on the QRL project's channels, with links to the release and its notes.
 - [ ] Lean back, wait for bug reports and repeat from step 1 :).
