# Warning set applied to every first-party target, and never to ext/.
#
# -Wconversion, -Wsign-conversion and -Wswitch-enum earn their keep in an
# emulator: silent width and signedness changes are exactly how a cycle count
# or an address wrap goes wrong on one platform and not another.

# Defaults ON, for every build type rather than debug and CI only. In an
# emulator a warning is rarely cosmetic: -Wconversion and -Wsign-conversion fire
# on exactly the silent width and signedness changes that make a cycle count or
# an address wrap differ between platforms, and this project's central claim is
# that emulated results are bit-identical across platforms and build types. A
# warning that is an error in Debug and a note in Release is a warning that gets
# through in Release.
#
# It applies only to first-party targets -- apollo_set_warnings() is never
# called on anything in ext/ -- so a warning in vendored code can never fail
# this build.
option(APOLLO_WERROR "Treat warnings as errors" ON)

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
