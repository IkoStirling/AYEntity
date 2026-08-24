# =============================================================================
# CheckNoBgfxInPublicHeaders.cmake — AYEntity mirror
#
# Audit F-5 guard for AYEntity (lh-rh-split-entity audit M4, 2026-08-24)。
# AYEntity 是 ECS + World scene 层 — public headers MUST NOT 引入 bgfx/bx。
#
# 与 AY2D/cmake/CheckNoBgfxInPublicHeaders.cmake 同形态，复用 AYTGT_*
# 参数化入口；本目录仅仅持有一份，因为 AYEntity 不应当有自己的"变种"，仅
# 是把模块名标识从 AY2D 改成 AYEntity 即可。
# =============================================================================

cmake_minimum_required(VERSION 3.20)

if(NOT AYTGT_PUBLIC_HEADER_DIR)
    message(FATAL_ERROR "[CheckNoBgfxInPublicHeaders] AYTGT_PUBLIC_HEADER_DIR not set")
endif()

string(REPLACE "\"" "" AYTGT_PUBLIC_HEADER_DIR "${AYTGT_PUBLIC_HEADER_DIR}")

if(NOT AYTGT_MODULE_NAME)
    # 与 CMakeLists.txt 显式传入同名；如果 caller 漏传，默认 = "AYEntity"。
    set(AYTGT_MODULE_NAME "AYEntity")
endif()

# AYEntity include/ 目录已存在并已 populate；guard load-bearing。
# 若目录被删了，STATUS 报错让 owner 注意到。
if(NOT EXISTS "${AYTGT_PUBLIC_HEADER_DIR}")
    message(STATUS
        "[CheckNoBgfxInPublicHeaders] (${AYTGT_MODULE_NAME}) "
        "Public header dir '${AYTGT_PUBLIC_HEADER_DIR}' does not exist; "
        "guard skipped (PR-scaffold / pre-Phase-1 state). "
        "Once populated, the guard becomes load-bearing.")
    return()
endif()

set(_AYTGT_LEAK_PATTERNS
    "<bgfx/"
    "<bx/"
    "\"bgfx/"
    "\"bx/"
)

set(_AYTGT_LEAK_HITS "")

file(GLOB_RECURSE _AYTGT_HEADERS
    "${AYTGT_PUBLIC_HEADER_DIR}/*.h"
    "${AYTGT_PUBLIC_HEADER_DIR}/*.hpp"
    "${AYTGT_PUBLIC_HEADER_DIR}/*.inl"
)

if(NOT _AYTGT_HEADERS)
    message(STATUS
        "[CheckNoBgfxInPublicHeaders] (${AYTGT_MODULE_NAME}) "
        "No headers under '${AYTGT_PUBLIC_HEADER_DIR}'. Guard trivially passed.")
    return()
endif()

foreach(_header IN LISTS _AYTGT_HEADERS)
    file(READ "${_header}" _AYTGT_CONTENT)

    foreach(_pattern IN LISTS _AYTGT_LEAK_PATTERNS)
        string(FIND "${_AYTGT_CONTENT}" "${_pattern}" _AYTGT_FOUND)
        if(_AYTGT_FOUND GREATER -1)
            list(APPEND _AYTGT_LEAK_HITS
                "  ${_header}  matched pattern: ${_pattern}")
            break()
        endif()
    endforeach()
endforeach()

if(_AYTGT_LEAK_HITS)
    list(LENGTH _AYTGT_LEAK_HITS _AYTGT_HIT_COUNT)
    message(SEND_ERROR
        "[CheckNoBgfxInPublicHeaders] (${AYTGT_MODULE_NAME}) "
        "Public-header bgfx/bx leak detected in ${_AYTGT_HIT_COUNT} file(s):\n"
        "${_AYTGT_LEAK_HITS}\n"
        "Public headers under '${AYTGT_PUBLIC_HEADER_DIR}' MUST NOT include\n"
        "<bgfx/*.h> or <bx/*.h> (design.md §0 / §3.1 / audit F-5). Move such\n"
        "includes to `src/detail/*` or private header paths.")
else()
    message(STATUS
        "[CheckNoBgfxInPublicHeaders] (${AYTGT_MODULE_NAME}) "
        "Public headers under '${AYTGT_PUBLIC_HEADER_DIR}' contain no bgfx/bx includes. "
        "Guard passed.")
endif()
