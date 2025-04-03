cmake_minimum_required(VERSION 3.5)
project(dumptorrent)
set(DUMPTORRENT_VERSION "1.6.0")
include_directories(${PROJECT_SOURCE_DIR}/include)

# ------------------------------------------------------------------------------
# dumptorrent target
# ------------------------------------------------------------------------------
set(DUMPTORRENT_SOURCES
    src/main.c
    src/torrent.c
    src/benc.c
    src/scrapec.c
    src/sha1.c
)

add_executable(dumptorrent
    ${DUMPTORRENT_SOURCES}
)
target_compile_definitions(dumptorrent
    PRIVATE
        DUMPTORRENT_VERSION="${DUMPTORRENT_VERSION}"
)

# ------------------------------------------------------------------------------
# scrapec target
# ------------------------------------------------------------------------------
set(SCRAPE_SOURCES
    src/scrapec.c
    src/benc.c
    src/sha1.c
)

add_executable(scrapec
    ${SCRAPE_SOURCES}
)

target_compile_definitions(scrapec
    PUBLIC
        BUILD_MAIN
        DUMPTORRENT_VERSION="${DUMPTORRENT_VERSION}"
)
