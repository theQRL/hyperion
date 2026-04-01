#!/usr/bin/env bash

set -e

## GLOBAL VARIABLES

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)

## FUNCTIONS

if [ "$CIRCLECI" ]
then
    function printTask { echo "$(tput bold)$(tput setaf 2)$1$(tput setaf 7)"; }
    function printError { echo "$(tput setaf 1)$1$(tput setaf 7)"; }
else
    function printTask { echo "$(tput bold)$(tput setaf 2)$1$(tput sgr0)"; }
    function printError { echo "$(tput setaf 1)$1$(tput sgr0)"; }
fi

printTask "Checking docs examples style"
HYPTMPDIR=$(mktemp -d)
(
    set -e
    cd "$HYPTMPDIR"
    "$REPO_ROOT"/scripts/isolate_tests.py "$REPO_ROOT"/docs/

    if npm -v >/dev/null 2>&1; then
        if npm list -g | grep hyphint >/dev/null 2>&1; then
            echo "node is installed, setting up hyphint"
            cp "$REPO_ROOT"/test/.hyphint.json "$HYPTMPDIR"/.hyphint.json
            cp "$REPO_ROOT"/test/.hyphintignore "$HYPTMPDIR"/.hyphintignore

            for f in *.hyp
            do
                echo "$f"
                # Only report errors
                hyphint -f unix "$HYPTMPDIR/$f"
            done
        else
            echo "node is installed, but not hyphint"
            exit 1
        fi
    else
        echo "node not installed, skipping docs style checker"
        exit 1
    fi
)
rm -rf "$HYPTMPDIR"
echo "Done."
