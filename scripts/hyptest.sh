#!/usr/bin/env bash
set -eu

REPO_ROOT="$(dirname "$0")"/..
USE_DEBUGGER=0
DEBUGGER="gdb --args"
BOOST_OPTIONS=()
HYPTEST_OPTIONS=()
HYPERION_BUILD_DIR=${HYPERION_BUILD_DIR:-${REPO_ROOT}/build}

function usage
{
	echo 2>&1 "
Usage: $0 [options] [hyptest-options]
Runs BOOST C++ unit test program, hyptest.

Options:
  --debug                  hyptest invocation prefaced with: \"$DEBUGGER\"
  --debugger *dbg-cmd*     hyptest prefaced with your own debugger command.
  --run_test | -t  *name*  filters test unit(s) to include or exclude from test.
                           This  option can be given several times.
  --boost-options *x*      Set BOOST option *x*.
  --show-progress | -p     Set BOOST option --show-progress.

Important environment variables:

HYPERION_BUILD_DIR: Sets directory where test/hyptest should be found.
           The default is \"${HYPERION_BUILD_DIR}\".
"
}

while [ $# -gt 0 ]
do
	case "$1" in
		--debugger)
			shift
			DEBUGGER="$1"
			USE_DEBUGGER=1
			;;
		--debug)
			USE_DEBUGGER=1
			;;
		--boost-options)
			shift
			BOOST_OPTIONS+=("$1")
			;;
		--help)
			usage
			exit 0
			;;
		--run_test | -t )
			shift
			BOOST_OPTIONS+=(-t "$1")
			;;
		--show-progress | -p)
			BOOST_OPTIONS+=("$1")
			;;
		*)
			HYPTEST_OPTIONS+=("$1")
			;;
	esac
	shift
done

HYPTEST_COMMAND=("${HYPERION_BUILD_DIR}/test/hyptest" "${BOOST_OPTIONS[@]}" -- --testpath "${REPO_ROOT}/test" "${HYPTEST_OPTIONS[@]}")

if [ "$USE_DEBUGGER" -ne "0" ]; then
	# shellcheck disable=SC2086
	exec ${DEBUGGER} "${HYPTEST_COMMAND[@]}"
else
	exec "${HYPTEST_COMMAND[@]}"
fi
