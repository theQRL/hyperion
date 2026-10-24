## Checklist for making a release:

### Requirements
 - [ ] GitHub account with access to [hyperion](https://github.com/theQRL/hyperion), [hypc-js](https://github.com/theQRL/hypc-js),
       [hypc-bin](https://github.com/theQRL/hypc-bin), [hyperion-website](https://github.com/theQRL/hyperion-website).
 - [ ] DockerHub account with push rights to the [``hypc`` image](https://hub.docker.com/r/theQRL/hypc).
 - [ ] Launchpad (Ubuntu One) account with a membership in the ["QRL" team](https://launchpad.net/~theqrl) and
       a gnupg key for your email in the ``theqrl.org`` domain (has to be version 1, gpg2 won't work).
 - [ ] Ubuntu/Debian dependencies of the PPA scripts: ``devscripts``, ``debhelper``, ``dput``, ``git``, ``wget``, ``ca-certificates``.
 - [ ] [npm Registry](https://www.npmjs.com) account with two-factor authentication, added as a maintainer of [``@theqrl/hypc``](https://www.npmjs.com/package/@theqrl/hypc).
       Publishing itself runs only from CI through npm trusted publishing.
 - [ ] A GPG or SSH signing key added to your GitHub account as a signing key, for signing release tags.
 - [ ] Access to the [hyperion_lang Twitter account](https://twitter.com/hyperion_lang).
 - [ ] [Reddit](https://www.reddit.com) account that is at least 10 days old with a minimum of 20 comment karma (``/r/QRL`` requirements).

### Pre-flight checks
At least a day before the release:
 - [ ] Run ``make linkcheck`` from within ``docs/`` and fix any broken links it finds.
       Ignore false positives caused by ``href`` anchors and dummy links not meant to work.
 - [ ] Double-check that the most recent Hyperion documentation builds succeeded.
 - [ ] Make sure that all merged PRs that should have changelog entries do have them.
 - [ ] Rerun CI on the top commits of main branches in all repositories that do not have daily activity by creating a test branch or PR:
      - [ ] ``hypc-js``
      - [ ] ``hypc-bin`` (make sure the bytecode comparison check did run)
 - [ ] (Optional) Create a prerelease in our Ubuntu PPA by following the steps in the PPA section below on ``develop`` rather than on a tag.
       This is recommended especially when dealing with PPA for the first time, when we add a new Ubuntu version or when the PPA scripts were modified in this release cycle.
 - [ ] Verify that the release tarball of ``hypc-js`` works.
       Bump version locally, add ``hypjson.js`` from CI, build it, compare the file structure with the previous version, install it locally and try to use it.

### Drafts
At least a day before the release:
 - [ ] Create a draft PR to sort the changelog.
 - [ ] Create draft PRs to bump version in ``hyperion`` and ``hypc-js``.
 - [ ] Prepare the release notes. Do not create a draft release on GitHub: publishing a draft whose tag
       does not exist yet creates an unsigned lightweight tag, which CI rejects and which burns the version number.
 - [ ] Create a draft PR to update ``hyperion-website``.
 - [ ] Create drafts of blog posts.
 - [ ] Prepare drafts of Twitter, Reddit and Hyperion Forum announcements.

### Blog Post
 - [ ] Create a post on [hyperion-website](https://github.com/theQRL/hyperion-website/tree/main/src/posts) in the ``Releases`` category and explain some of the new features or concepts.
 - [ ] Create a post on [hyperion-website](https://github.com/theQRL/hyperion-website/tree/main/src/posts) in the ``Security Alerts`` category in case of important bug(s).

### Changelog
 - [ ] Sort the changelog entries alphabetically and correct any errors you notice. Commit it.
 - [ ] Update the changelog to include a release date.
 - [ ] Run ``scripts/update_bugs_by_version.py`` to regenerate ``bugs_by_version.json`` from the changelog and ``bugs.json``.
       Make sure that the resulting ``bugs_by_version.json`` has a new, empty entry for the new version.
 - [ ] Commit changes, create a pull request and wait for the tests. Then merge it.
 - [ ] Copy the changelog into the release blog post.

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
       ``hypc-static-linux-arm`` (ARM64), ``hypjson.js`` and ``SHA256SUMS``.
 - [ ] Update hypc-bin separately using the binaries from the same tagged GitHub Release:
       run ``node scripts/add-release.mjs $VERSION`` in ``hypc-bin``. It downloads the assets, checks them against the
       release ``SHA256SUMS`` and the build provenance attestation, and regenerates ``bin/list.json``.
 - [ ] Create a pull request in hypc-bin, wait for its checks and a review, and merge.

### Homebrew and MacOS
 - [ ] Update the version and the hash (``sha256sum hyperion_$VERSION.tar.gz``) in the [``hyperion`` formula in Homebrew core repository](https://github.com/Homebrew/homebrew-core/blob/master/Formula/hyperion.rb).

### Docker
 - [ ] Run ``./scripts/docker_deploy_manual.sh v$VERSION``.

### PPA
 - [ ] Create ``.release_ppa_auth`` at the root of your local Hyperion checkout and set ``LAUNCHPAD_EMAIL`` and ``LAUNCHPAD_KEYID`` to your key's email and key id.
 - [ ] Double-check that the ``DISTRIBUTIONS`` list in ``scripts/release_ppa.sh`` and ``scripts/deps-ppa/static_z3.sh`` contains the most recent versions of Ubuntu.
 - [ ] Make sure the QRL ``cpp-build-deps`` PPA repository contains ``libz3-static-dev builds`` for all current versions of Ubuntu.
       Note that it may be included in the ``z3-static`` multipackage (follow the ``View package details`` link to check).
       If not present, run ``scripts/deps-ppa/static_z3.sh`` and wait for the builds to succeed before continuing.
 - [ ] Run ``scripts/release_ppa.sh v$VERSION`` to create the PPA release.
       This will create a single package containing static binary for older Ubuntu versions in the QRL static PPA
       and separate packages with dynamically-linked binaries for recent versions (those listed in ``DISTRIBUTIONS``) in the QRL release PPA.
 - [ ] Wait for the build to be finished and published for *all architectures* (currently we only build for ``amd64``, but we may add ``arm`` in the future).
       **SERIOUSLY: DO NOT PROCEED EARLIER!!!**
 - [ ] *After* the package with the static build is *published*, use it to create packages for older Ubuntu versions.
       Copy the static package to the QRL release PPA
       for the destination series ``Trusty``, ``Xenial`` and ``Bionic`` while selecting ``Copy existing binaries``.

### Release hypc-js
Follow [``RELEASE.md`` in hypc-js](https://github.com/theQRL/hypc-js/blob/main/RELEASE.md). In short:
 - [ ] Wait until hypc-bin was properly deployed. You can test this via remix - a test run through remix is advisable anyway.
 - [ ] Open a pull request that sets ``"version"`` in ``package.json`` and runs ``npm run updateBinary``.
       Merge it after its checks pass and it has been reviewed.
 - [ ] Tag the merge commit with a signed tag and push only that tag: ``git tag -s v$VERSION -m v$VERSION && git push origin v$VERSION``.
 - [ ] Ask another maintainer to approve the ``npm-publish`` environment.
       The ``Release`` workflow then publishes the tarball to npm with provenance and creates the GitHub release.
 - [ ] Never run ``npm publish`` or ``npm run publish:tarball`` by hand.

### Post-release
 - [ ] Make sure the documentation for the new release has been published successfully.
       Verify that the new documentation version is listed, works and is marked as default.
 - [ ] Remove "still in progress" warning from the [release notes](https://github.com/theQRL/hyperion/releases).
 - [ ] Merge the [blog posts](https://github.com/theQRL/hyperion-website/pulls) related to the release.
 - [ ] Create a commit to increase the version number on ``develop`` in ``CMakeLists.txt`` and add a new skeleton changelog entry.
 - [ ] Update the release information section [in the hyperion-website source](https://github.com/theQRL/hyperion-website/blob/main/src/pages/index.tsx).
 - [ ] Announce on [Twitter](https://twitter.com/hyperion_lang), including links to the release and the blog post.
 - [ ] Announce on QRL project channels, including links to the release and the blog post.
 - [ ] Share the announcement on Reddit in [``/r/QRL``](https://reddit.com/r/QRL/).
 - [ ] Share the announcement in Hyperion project channels.
 - [ ] Share the announcement on [Project Updates](https://discord.com/channels/420394352083337236/798974456704925696)
 - [ ] Share the announcement on [`#theqrl_hyperion` channel on Matrix](https://matrix.to/#/#theqrl_hyperion:gitter.im)
 - [ ] Share the announcement on [`#hypc-tooling`](https://matrix.to/#/#hypc-tooling:matrix.org)
 - [ ] Lean back, wait for bug reports and repeat from step 1 :).
