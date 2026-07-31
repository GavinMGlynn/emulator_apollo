# Warning set applied to every first-party target, and never to ext/.
#
# -Wconversion, -Wsign-conversion and -Wswitch-enum earn their keep in an
# emulator: silent width and signedness changes are exactly how a cycle count
# or an address wrap goes wrong on one platform and not another.

option(APOLLO_WERROR "Treat warnings as errors" OFF)

function(apollo_set_warnings target)
  set(_warnings
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wcast-qual
    -Wcast-align
    -Wpointer-arith
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wredundant-decls
    -Wundef
    -Wwrite-strings
    -Wdouble-promotion
    -Wformat=2
    -Wswitch-enum
    -Wvla
  )

  if(MSVC)
    # clang-cl accepts the clang flags; a true MSVC frontend does not.
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE ${_warnings})
  endif()

  if(APOLLO_WERROR)
    if(MSVC)
      target_compile_options(${target} PRIVATE /WX)
    else()
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()

  if(WIN32)
    # Without this the UCRT deprecation storm buries real warnings.
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endfunction()
