# generates BuildInfo.h
#
# this module expects
# ZOND_SOURCE_DIR - main CMAKE_SOURCE_DIR
# ZOND_DST_DIR - main CMAKE_BINARY_DIR
# ZOND_BUILD_TYPE
# ZOND_BUILD_PLATFORM
#
# example usage:
# cmake -DZOND_SOURCE_DIR=. -DZOND_DST_DIR=build -DZOND_BUILD_TYPE=Debug -DZOND_BUILD_PLATFORM=Darwin.appleclang -P scripts/buildinfo.cmake
#
# Its main output variables are HYP_VERSION_BUILDINFO and HYP_VERSION_PRERELEASE

if (NOT ZOND_BUILD_TYPE)
	set(ZOND_BUILD_TYPE "unknown")
endif()

if (NOT ZOND_BUILD_PLATFORM)
	set(ZOND_BUILD_PLATFORM "unknown")
endif()

# Logic here: If prerelease.txt exists but is empty, it is a non-pre release.
# If it does not exist, create our own prerelease string
if (EXISTS ${ZOND_SOURCE_DIR}/prerelease.txt)
	file(READ ${ZOND_SOURCE_DIR}/prerelease.txt HYP_VERSION_PRERELEASE)
	string(STRIP "${HYP_VERSION_PRERELEASE}" HYP_VERSION_PRERELEASE)
else()
	string(TIMESTAMP HYP_VERSION_PRERELEASE "develop.%Y.%m.%d" UTC)
	string(REPLACE .0 . HYP_VERSION_PRERELEASE "${HYP_VERSION_PRERELEASE}")
endif()

if (EXISTS ${ZOND_SOURCE_DIR}/commit_hash.txt)
	file(READ ${ZOND_SOURCE_DIR}/commit_hash.txt HYP_COMMIT_HASH)
	string(STRIP ${HYP_COMMIT_HASH} HYP_COMMIT_HASH)
else()
	execute_process(
		COMMAND git --git-dir=${ZOND_SOURCE_DIR}/.git --work-tree=${ZOND_SOURCE_DIR} rev-parse --short=8 HEAD
		OUTPUT_VARIABLE HYP_COMMIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
	)
	execute_process(
		COMMAND git --git-dir=${ZOND_SOURCE_DIR}/.git --work-tree=${ZOND_SOURCE_DIR} diff HEAD --shortstat
		OUTPUT_VARIABLE HYP_LOCAL_CHANGES OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
	)
endif()

if (HYP_COMMIT_HASH)
	string(STRIP ${HYP_COMMIT_HASH} HYP_COMMIT_HASH)
	string(SUBSTRING ${HYP_COMMIT_HASH} 0 8 HYP_COMMIT_HASH)
endif()

if (NOT HYP_COMMIT_HASH)
	message(FATAL_ERROR "Unable to determine commit hash. Either compile from within git repository or "
		"supply a file called commit_hash.txt")
endif()
if (NOT HYP_COMMIT_HASH MATCHES [a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9][a-f0-9])
    message(FATAL_ERROR "Malformed commit hash \"${HYP_COMMIT_HASH}\". It has to consist of exactly 8 hex digits.")
endif()

if (HYP_COMMIT_HASH AND HYP_LOCAL_CHANGES)
	set(HYP_COMMIT_HASH "${HYP_COMMIT_HASH}.mod")
endif()

set(HYP_VERSION_COMMIT "commit.${HYP_COMMIT_HASH}")
set(HYP_VERSION_PLATFORM ZOND_BUILD_PLATFORM)
set(HYP_VERSION_BUILDINFO "commit.${HYP_COMMIT_HASH}.${ZOND_BUILD_PLATFORM}")

set(TMPFILE "${ZOND_DST_DIR}/BuildInfo.h.tmp")
set(OUTFILE "${ZOND_DST_DIR}/BuildInfo.h")

configure_file("${ZOND_BUILDINFO_IN}" "${TMPFILE}")

include("${ZOND_CMAKE_DIR}/ZondUtils.cmake")
replace_if_different("${TMPFILE}" "${OUTFILE}" CREATE)

