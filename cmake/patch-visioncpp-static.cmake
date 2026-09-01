# Applied by FetchContent PATCH_COMMAND (working directory = vision.cpp source
# root). Upstream vision.cpp's VISP_API macro only knows VISP_API_EXPORT
# (dllexport when building the library itself); on MSVC every other consumer
# gets __declspec(dllimport), which cannot be linked against a static library
# (LNK2019 on "__imp_..." symbols). Add standard "<NAME>_STATIC_DEFINE"
# support so that defining VISP_STATIC_DEFINE yields a plain (empty) VISP_API
# on both sides. Safe to run multiple times (idempotent).

set(file include/visp/util.h)
file(READ "${file}" content)

if(NOT content MATCHES "VISP_STATIC_DEFINE")
  string(
    REPLACE
    "#    ifdef VISP_API_EXPORT"
    "#    ifdef VISP_STATIC_DEFINE\n#        define VISP_API\n#    elif defined(VISP_API_EXPORT)"
    content "${content}"
  )
  file(WRITE "${file}" "${content}")
endif()
