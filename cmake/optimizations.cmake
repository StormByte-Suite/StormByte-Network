# Optimizations
#
# Edit the three lists. On Release they are appended to the Release flag
# cache, after the compiler defaults, so -O2 / /O2 wins over a default
# -O3 / /O3. A language is left alone when its flags were set from the
# outside: -DCMAKE_C_FLAGS, -DCMAKE_CXX_FLAGS, the matching
# CMAKE_<LANG>_FLAGS_RELEASE, or CFLAGS / CXXFLAGS in the environment.
# -stdlib=libc++ and -stdlib=libstdc++ do not count: they only choose
# the C++ runtime. If either is the only extra flag, Release still
# receives the lists below.
# CMake copies the environment into both the flags and their _INIT value,
# so comparing those two cannot see it.
#
# Linux is x86-64-v3 (AVX2+FMA+BMI). Apple is apple-m1. Windows is
# /O2 /arch:AVX. No /fp:fast and no /EHsc.

option(ENABLE_LTO "Link-time optimization. Not tied to the build type. Combines with ENABLE_PGO." ON)

set(OPTIMIZATION_FLAGS_LINUX "-O2;-march=x86-64-v3" CACHE STRING "Extra C/CXX flags on Linux. Semicolon-separated.")
set(OPTIMIZATION_FLAGS_MAC "-O2;-mcpu=apple-m1" CACHE STRING "Extra C/CXX flags on Apple. Semicolon-separated.")
set(OPTIMIZATION_FLAGS_WINDOWS "/O2;/arch:AVX" CACHE STRING "Extra C/CXX flags on Windows. Semicolon-separated.")

if(WIN32)
	set(_sb_opt_flags "${OPTIMIZATION_FLAGS_WINDOWS}")
elseif(APPLE)
	set(_sb_opt_flags "${OPTIMIZATION_FLAGS_MAC}")
else()
	set(_sb_opt_flags "${OPTIMIZATION_FLAGS_LINUX}")
endif()

set(_sb_opt_release FALSE)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
	set(_sb_opt_release TRUE)
elseif(CMAKE_CONFIGURATION_TYPES)
	list(FIND CMAKE_CONFIGURATION_TYPES "Release" _sb_opt_rel_idx)
	if(NOT _sb_opt_rel_idx EQUAL -1)
		set(_sb_opt_release TRUE)
	endif()
endif()

# The C++ runtime selector is not a custom optimization flag.
function(_sb_opt_ignore_stdlib value out)
	set(_s " ${value} ")
	foreach(_pat IN ITEMS "-stdlib=libc\\+\\+" "-stdlib=libstdc\\+\\+")
		string(REGEX REPLACE "(^| )${_pat}( |$)" " " _s "${_s}")
		string(REGEX REPLACE " +" " " _s "${_s}")
	endforeach()
	string(STRIP "${_s}" _s)
	set(${out} "${_s}" PARENT_SCOPE)
endfunction()

if(_sb_opt_release)
	foreach(_sb_opt_lang C CXX)
		if(_sb_opt_lang STREQUAL "C")
			string(STRIP "$ENV{CFLAGS}" _sb_opt_env)
		else()
			string(STRIP "$ENV{CXXFLAGS}" _sb_opt_env)
		endif()
		set(_sb_opt_base "CMAKE_${_sb_opt_lang}_FLAGS")
		set(_sb_opt_rel "CMAKE_${_sb_opt_lang}_FLAGS_RELEASE")
		string(STRIP "${${_sb_opt_base}}" _sb_opt_now)
		string(STRIP "${${_sb_opt_base}_INIT}" _sb_opt_init)
		string(STRIP "${${_sb_opt_rel}}" _sb_opt_rel_now)
		string(STRIP "${${_sb_opt_rel}_INIT}" _sb_opt_rel_init)
		# Keep the real Release flags. The copies below are only for the
		# "did the user pass something else?" test.
		set(_sb_opt_rel_keep "${_sb_opt_rel_now}")
		_sb_opt_ignore_stdlib("${_sb_opt_env}" _sb_opt_env)
		_sb_opt_ignore_stdlib("${_sb_opt_now}" _sb_opt_now)
		_sb_opt_ignore_stdlib("${_sb_opt_init}" _sb_opt_init)
		_sb_opt_ignore_stdlib("${_sb_opt_rel_now}" _sb_opt_rel_now)
		_sb_opt_ignore_stdlib("${_sb_opt_rel_init}" _sb_opt_rel_init)
		if(_sb_opt_env STREQUAL "" AND _sb_opt_now STREQUAL _sb_opt_init AND _sb_opt_rel_now STREQUAL _sb_opt_rel_init)
			set(_sb_opt_value "${_sb_opt_rel_keep}")
			foreach(_sb_opt_flag IN LISTS _sb_opt_flags)
				if(NOT _sb_opt_flag STREQUAL "")
					string(APPEND _sb_opt_value " ${_sb_opt_flag}")
				endif()
			endforeach()
			string(STRIP "${_sb_opt_value}" _sb_opt_value)
			get_property(_sb_opt_help CACHE "${_sb_opt_rel}" PROPERTY HELPSTRING)
			if(NOT _sb_opt_help)
				set(_sb_opt_help "${_sb_opt_rel}")
			endif()
			set("${_sb_opt_rel}" "${_sb_opt_value}" CACHE STRING "${_sb_opt_help}" FORCE)
		endif()
	endforeach()
endif()

unset(_sb_opt_flags)
unset(_sb_opt_release)
unset(_sb_opt_rel_idx)
unset(_sb_opt_lang)
unset(_sb_opt_env)
unset(_sb_opt_base)
unset(_sb_opt_rel)
unset(_sb_opt_now)
unset(_sb_opt_init)
unset(_sb_opt_rel_now)
unset(_sb_opt_rel_keep)
unset(_sb_opt_rel_init)
unset(_sb_opt_value)
unset(_sb_opt_flag)
unset(_sb_opt_help)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION "${ENABLE_LTO}")
foreach(_lto_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_${_lto_cfg} "${ENABLE_LTO}")
endforeach()
unset(_lto_cfg)
