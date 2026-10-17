# Windows CI needs a Hunter release that recognizes MSVC 19.4x.
set(HUNTER_CONFIGURATION_TYPES Release CACHE STRING "Build type of Hunter packages")
set(HUNTER_USE_CACHE_SERVERS NO CACHE STRING "Download binaries from Hunter cache servers")

include(HunterGate)

# Keep QRVM's local package configuration and dependency versions.
HunterGate(
    URL "https://github.com/cpp-pm/hunter/archive/refs/tags/v0.26.12.tar.gz"
    SHA1 "6498c5d0dec25d7fffb2b0574ebaf265179b894d"
    LOCAL
)
