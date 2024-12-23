#!/usr/bin/env bash

set -e

READLINK=readlink
if [[ "$OSTYPE" == "darwin"* ]]; then
    READLINK=greadlink
fi
ROOT_DIR=$(${READLINK} -f "$(dirname "$0")"/..)
WORKDIR="${ROOT_DIR}/build/antlr"
ANTLR_JAR="${ROOT_DIR}/build/deps/antlr4.jar"
ANTLR_JAR_URI="https://www.antlr.org/download/antlr-4.8-complete.jar"

SGR_RESET="\033[0m"
SGR_BOLD="\033[1m"
SGR_GREEN="\033[32m"
SGR_RED="\033[31m"
SGR_BLUE="\033[34m"

vt_cursor_up() { echo -ne "\033[A"; }
vt_cursor_begin_of_line() { echo -ne "\r"; }

function download_antlr4
{
  if [[ ! -e "$ANTLR_JAR" ]]
  then
    curl -o "${ANTLR_JAR}" "${ANTLR_JAR_URI}"
  fi
}

function prepare_workdir
{
  mkdir -p "${ROOT_DIR}/build/deps"
  mkdir -p "${WORKDIR}"
  mkdir -p "${WORKDIR}/src"
  mkdir -p "${WORKDIR}/target"
}

prepare_workdir
download_antlr4

echo "Creating parser"
(
cd "${ROOT_DIR}"/docs/grammar
# Create lexer/parser from grammar
java -jar "${ANTLR_JAR}" HyperionParser.g4 HyperionLexer.g4 -o "${WORKDIR}/src/"

# Compile lexer/parser sources
javac -classpath "${ANTLR_JAR}" "${WORKDIR}/src/"*.java -d "${WORKDIR}/target/"
)

# Run tests
failed_count=0
function test_file
{
  local HYP_FILE
  HYP_FILE="$(${READLINK}  -m "${1}")"
  local cur=${2}
  local max=${3}
  local solOrYul=${4}

  echo -e "${SGR_BLUE}[${cur}/${max}] Testing ${HYP_FILE}${SGR_RESET} ..."
  local output
  if [[ "${solOrYul}" == "sol" ]]; then
    output=$(
      grep -v "^==== ExternalSource:" "${HYP_FILE}" | java \
        -classpath "${ANTLR_JAR}:${WORKDIR}/target/" \
        "org.antlr.v4.gui.TestRig" \
        Hyperion \
        sourceUnit 2>&1
    )
  else
    output=$(
      echo "assembly $(cat "${HYP_FILE}")" | java \
        -classpath "${ANTLR_JAR}:${WORKDIR}/target/" \
        "org.antlr.v4.gui.TestRig" \
        Hyperion \
        assemblyStatement 2>&1
    )
  fi
  vt_cursor_up
  vt_cursor_begin_of_line
  if grep -qE "^\/\/ ParserError" "${HYP_FILE}"; then
    if [[ "${output}" != "" ]]
    then
      echo -e "${SGR_BLUE}[${cur}/${max}] Testing ${HYP_FILE}${SGR_RESET} ${SGR_BOLD}${SGR_GREEN}FAILED AS EXPECTED${SGR_RESET}"
    else
      echo -e "${SGR_BLUE}[${cur}/${max}] Testing ${HYP_FILE}${SGR_RESET} ${SGR_BOLD}${SGR_RED}SUCCEEDED DESPITE PARSER ERROR${SGR_RESET}"
      echo "${output}"
      failed_count=$((failed_count + 1))
      exit 1
    fi
  else
    if [[ "${output}" == "" ]]
    then
      echo -e "${SGR_BLUE}[${cur}/${max}] Testing ${HYP_FILE}${SGR_RESET} ${SGR_BOLD}${SGR_GREEN}OK${SGR_RESET}"
    else
      echo -e "${SGR_BLUE}[${cur}/${max}] Testing ${HYP_FILE}${SGR_RESET} ${SGR_BOLD}${SGR_RED}FAILED${SGR_RESET}"
      echo "${output}"
      failed_count=$((failed_count + 1))
      exit 1
    fi
  fi
}

# we only want to use files that do not contain excluded parser errors, analysis errors or multi-source files.
HYP_FILES=()
while IFS='' read -r line
do
  HYP_FILES+=("$line")
done < <(
  grep --include "*.hyp" -riL -E \
    "^\/\/ (Syntax|Type|Declaration)Error|^\/\/ ParserError (1684|2837|3716|3997|5333|6275|6281|6933|7319|8185|7637)|^==== Source:|^pragma experimental hyperion;" \
    "${ROOT_DIR}/test/libhyperion/syntaxTests" \
    "${ROOT_DIR}/test/libhyperion/semanticTests" |
      # Skipping the unicode tests as I couldn't adapt the lexical grammar to recursively counting RLO/LRO/PDF's.
      grep -v -E 'comments/.*_direction_override.*.hyp' |
      grep -v -E 'literals/.*_direction_override.*.hyp' |
      # Skipping a test with "revert E;" because ANTLR cannot distinguish it from
      # a variable declaration.
      grep -v -E 'revertStatement/non_called.hyp' |
      # Skipping tests with "let prevrandao := ..."
      grep -v -E 'inlineAssembly/prevrandao_disallowed_function_post_paris.hyp' |
      # Skipping license error, unrelated to the grammar
      grep -v -E 'license/license_double5.hyp' |
      grep -v -E 'license/license_hidden_unicode.hyp' |
      grep -v -E 'license/license_unicode.hyp' |
      # Skipping tests with 'something.address' as 'address' as the grammar fails on those
      grep -v -E 'inlineAssembly/external_function_pointer_address.*.hyp'
)

YUL_FILES=()
# Add all yul optimizer tests without objects and types.
while IFS='' read -r line
do
  YUL_FILES+=("$line")
done < <(
  grep -riL -E \
    "object|\:[ ]*[uib]" \
    "${ROOT_DIR}/test/libyul/yulOptimizerTests"
)

num_tests=$((${#HYP_FILES[*]} + ${#YUL_FILES[*]}))
test_count=0
for HYP_FILE in "${HYP_FILES[@]}"
do
  test_count=$((test_count + 1))
  test_file "${HYP_FILE}" ${test_count} $num_tests "sol"
done
for YUL_FILE in "${YUL_FILES[@]}"
do
  test_count=$((test_count + 1))
  test_file "${YUL_FILE}" ${test_count} $num_tests "yul"
done

echo "Summary: ${failed_count} of $num_tests sources failed."
exit ${failed_count}
