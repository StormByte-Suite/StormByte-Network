# Profile-guided optimization, as one include.
#
# include() this file once, after project() and before any targets or install().
# -DENABLE_PGO=ON keeps this build directory as a normal build. It is the one
# cmake --install ships, and the project's install() rules are unchanged:
#   1. When this file is included, and before it returns, build an instrumented
#      tree in <build>/pgo-instr (static dependencies included) and run ctest.
#   2. Only then does the rest of this configure run. It compiles without tests,
#      using the profile training just wrote.
# This directory cannot rebuild itself from inside cmake --build, so training
# is not a ninja target. It happens at configure time, before thirdparty is
# added. The parent CMakeLists must include this file before add_subdirectory
# of thirdparty, lib or test. A later cmake run trains again, then configures
# this directory against the new profile.
# Flags go into CMAKE_C_FLAGS, CMAKE_CXX_FLAGS and the exe/shared/module linker
# flags. BuildMaster copies those into every nested static library. Directory
# compile options never reach that cmake, so the .a used to be built with
# neither -fprofile-generate nor -fprofile-use. The instrumented tree gets the
# generate flags. This directory gets the use flags only after training, so
# Clang can open a real profdata and no empty seed is required. Profile flags
# are stripped before they are forwarded. A flag that differs on one side
# makes GCC miss the .gcda and makes Clang discard the counters.
# Training and this configure touch the same third-party checkouts in
# sequence, not in parallel. BuildMaster resets and reapplies patches on each
# configure.
#
# The instrumented tree forces the cache option named by PGO_TEST_OPTION
# (default ENABLE_TEST) ON. This directory forces it OFF.
# Both trees get the same cache. Every project option (WITH_GPL, ENABLE_LTO,
# BUILD_SHARED_LIBS, ...) is forwarded. C and C++ flags are forwarded only
# after profile tokens are removed, so the instrumented tree cannot inherit
# -fprofile-use. The instrumented configure prints each forwarded variable.
# A value that points into this build directory is not forwarded and is
# printed as skipped: the child must not write into the parent tree.
# PGO_FORWARD_CACHE is an extra allow-list for a name the filter would drop.
#
# Copy this file into another project and include it the same way.
# GCC, Clang, clang-cl and MSVC are supported. Release is the useful config.

option(ENABLE_PGO "Train in pgo-instr, then compile this directory with the profile" OFF)

if(NOT ENABLE_PGO)
	return()
endif()

if(NOT PGO_TEST_OPTION)
	set(PGO_TEST_OPTION ENABLE_TEST)
endif()
if(NOT DEFINED PGO_FORWARD_CACHE)
	set(PGO_FORWARD_CACHE BUILD_SHARED_LIBS ENABLE_DOC)
endif()
if(NOT PGO_PROFILE_DIR)
	set(PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo-data")
endif()
if(NOT PGO_INSTR_DIR)
	set(PGO_INSTR_DIR "${CMAKE_BINARY_DIR}/pgo-instr")
endif()
file(MAKE_DIRECTORY "${PGO_PROFILE_DIR}")

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	set(_pgo_kind MSVC)
elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	set(_pgo_kind CLANG_CL)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	set(_pgo_kind CLANG)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(_pgo_kind GNU)
else()
	message(FATAL_ERROR "ENABLE_PGO does not support the ${CMAKE_CXX_COMPILER_ID} compiler")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	message(WARNING "ENABLE_PGO on a Debug build: the profile will not match an optimized binary")
endif()

# MSVC PGO is LTCG. CMake's own bare /LTCG replaces the profile mode, so IPO
# stays off. /GENPROFILE and /USEPROFILE alone fail with LNK1220: they require
# an /LTCG specification. The combined flags are that specification.
if(_pgo_kind STREQUAL "MSVC")
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
	foreach(_pgo_cfg RELEASE RELWITHDEBINFO MINSIZEREL DEBUG)
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_${_pgo_cfg} OFF)
	endforeach()
endif()

# clang-cl links with lld-link. The profile runtime is not on the default LIB path.
# -print-runtime-dir is the per-target dir (lib/<triple>). The Windows installer
# still ships the library under lib/windows.
function(_pgo_link_clang_profile_runtime)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
		set(_arch aarch64)
	elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
		set(_arch i386)
	else()
		set(_arch x86_64)
	endif()
	set(_name "clang_rt.profile-${_arch}.lib")
	set(_lib "")

	foreach(_flag IN ITEMS "-print-file-name=${_name}" "/clang:-print-file-name=${_name}")
		if(_lib)
		else()
			execute_process(
				COMMAND "${CMAKE_CXX_COMPILER}" "${_flag}"
				OUTPUT_VARIABLE _printed
				OUTPUT_STRIP_TRAILING_WHITESPACE
				ERROR_QUIET
			)
			string(STRIP "${_printed}" _printed)
			if(IS_ABSOLUTE "${_printed}" AND EXISTS "${_printed}")
				set(_lib "${_printed}")
			endif()
		endif()
	endforeach()

	if(NOT _lib)
		set(_res "")
		foreach(_flag IN ITEMS "-print-resource-dir" "/clang:-print-resource-dir" "-print-runtime-dir" "/clang:-print-runtime-dir")
			if(_res)
			else()
				execute_process(
					COMMAND "${CMAKE_CXX_COMPILER}" "${_flag}"
					OUTPUT_VARIABLE _printed
					OUTPUT_STRIP_TRAILING_WHITESPACE
					ERROR_QUIET
				)
				string(STRIP "${_printed}" _printed)
				file(TO_CMAKE_PATH "${_printed}" _printed)
				if(EXISTS "${_printed}")
					set(_res "${_printed}")
				endif()
			endif()
		endforeach()
		if(_res MATCHES "/lib/[^/]+$")
			get_filename_component(_libroot "${_res}" DIRECTORY)
			get_filename_component(_resource "${_libroot}" DIRECTORY)
		else()
			set(_resource "${_res}")
			set(_libroot "${_res}/lib")
		endif()
		foreach(_candidate IN ITEMS
			"${_libroot}/windows/${_name}"
			"${_resource}/lib/windows/${_name}"
			"${_libroot}/x86_64-pc-windows-msvc/${_name}"
			"${_resource}/lib/x86_64-pc-windows-msvc/${_name}"
			"${_res}/${_name}"
		)
			if(NOT _lib AND EXISTS "${_candidate}")
				set(_lib "${_candidate}")
			endif()
		endforeach()
	endif()

	if(NOT EXISTS "${_lib}")
		message(FATAL_ERROR "ENABLE_PGO: clang profile runtime not found (looked for ${_name})")
	endif()
	get_filename_component(_dir "${_lib}" DIRECTORY)
	file(TO_CMAKE_PATH "${_dir}" _dir)
	file(TO_CMAKE_PATH "${_lib}" _lib)
	add_link_options("SHELL:/LIBPATH:\"${_dir}\"" "${_lib}")
endfunction()

# Drop profile tokens so a reconfigure does not forward -fprofile-use into the
# instrumented tree, and so CMAKE_*_FLAGS is not skipped for pointing at this
# build directory. The use flags contain the build path.
function(_pgo_strip_profile_flags value out)
	set(_s " ${value} ")
	foreach(_pat IN ITEMS
		"-fprofile-generate=[^ ]+"
		"-fprofile-use=[^ ]+"
		"-fprofile-correction"
		"-fprofile-update=[^ ]+"
		"-fprofile-prefix-path=[^ ]+"
		"-Wno-error=missing-profile"
		"-Wno-error=coverage-mismatch"
		"-Wno-error=profile-instr-unprofiled"
		"-Wno-error=profile-instr-out-of-date"
		"-Wno-profile-instr-unprofiled"
		"-Wno-profile-instr-out-of-date"
		"/clang:-fprofile-generate=[^ ]+"
		"/clang:-fprofile-use=[^ ]+"
		"/clang:-Wno-error=profile-instr-unprofiled"
		"/clang:-Wno-error=profile-instr-out-of-date"
		"/clang:-Wno-profile-instr-unprofiled"
		"/clang:-Wno-profile-instr-out-of-date"
		"/LTCG:PGINSTRUMENT"
		"/LTCG:PGOPTIMIZE"
		"/GL"
		"/LIBPATH:\"[^\"]*/clang/[^\"]*\""
		"[^ ]*clang_rt\\.profile[^ ]*"
	)
		string(REGEX REPLACE "(^| )${_pat}( |$)" " " _s "${_s}")
		string(REGEX REPLACE " +" " " _s "${_s}")
	endforeach()
	string(STRIP "${_s}" _s)
	set(${out} "${_s}" PARENT_SCOPE)
endfunction()

# Write one stage's flags into the variables BuildMaster forwards. A macro, not a
# function: the include runs in the top-level directory, and a function's
# PARENT_SCOPE would write above that directory. The normal variable must match
# the cache, or it hides the flags from this directory's own compilations.
macro(_pgo_apply_stage_flags stage)
	set(_compile "")
	set(_link "")
	if("${stage}" STREQUAL "generate")
		if(_pgo_kind STREQUAL "GNU")
			list(APPEND _compile
				"-fprofile-generate=${PGO_PROFILE_DIR}"
				"-fprofile-update=atomic"
				"-fprofile-prefix-path=${CMAKE_BINARY_DIR}")
			list(APPEND _link "-fprofile-generate=${PGO_PROFILE_DIR}")
		elseif(_pgo_kind STREQUAL "CLANG")
			list(APPEND _compile "-fprofile-generate=${PGO_PROFILE_DIR}")
			list(APPEND _link "-fprofile-generate=${PGO_PROFILE_DIR}")
		elseif(_pgo_kind STREQUAL "CLANG_CL")
			# lld-link is the linker, not clang-cl. A /clang: flag is an input file
			# to lld-link. Instrumentation stays in the objects; the link only needs
			# the runtime, added below for targets in this tree.
			list(APPEND _compile "/clang:-fprofile-generate=${PGO_PROFILE_DIR}")
			_pgo_link_clang_profile_runtime()
		else()
			list(APPEND _compile "/GL")
			list(APPEND _link "/LTCG:PGINSTRUMENT")
		endif()
	else()
		if(_pgo_kind STREQUAL "GNU")
			list(APPEND _compile
				"-fprofile-use=${PGO_PROFILE_DIR}"
				"-fprofile-correction"
				"-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
				"-Wno-error=missing-profile"
				"-Wno-error=coverage-mismatch")
			list(APPEND _link "-fprofile-use=${PGO_PROFILE_DIR}")
		elseif(_pgo_kind STREQUAL "CLANG")
			list(APPEND _compile
				"-fprofile-use=${PGO_PROFILE_DIR}/default.profdata"
				"-Wno-error=profile-instr-unprofiled"
				"-Wno-error=profile-instr-out-of-date")
			list(APPEND _link "-fprofile-use=${PGO_PROFILE_DIR}/default.profdata")
		elseif(_pgo_kind STREQUAL "CLANG_CL")
			list(APPEND _compile
				"/clang:-fprofile-use=${PGO_PROFILE_DIR}/default.profdata"
				"/clang:-Wno-error=profile-instr-unprofiled"
				"/clang:-Wno-error=profile-instr-out-of-date")
		else()
			list(APPEND _compile "/GL")
			list(APPEND _link "/LTCG:PGOPTIMIZE")
		endif()
	endif()

	foreach(var IN ITEMS CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
		_pgo_strip_profile_flags("${${var}}" _base)
		set(_joined "${_base}")
		foreach(flag IN LISTS _compile)
			string(APPEND _joined " ${flag}")
		endforeach()
		string(STRIP "${_joined}" _joined)
		set(${var} "${_joined}" CACHE STRING "" FORCE)
		set(${var} "${_joined}")
	endforeach()
	foreach(var IN ITEMS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
		_pgo_strip_profile_flags("${${var}}" _base)
		set(_joined "${_base}")
		foreach(flag IN LISTS _link)
			string(APPEND _joined " ${flag}")
		endforeach()
		string(STRIP "${_joined}" _joined)
		set(${var} "${_joined}" CACHE STRING "" FORCE)
		set(${var} "${_joined}")
	endforeach()
	unset(_compile)
	unset(_link)
	unset(_base)
	unset(_joined)
	unset(var)
	unset(flag)
endmacro()

if(PGO_STAGE STREQUAL "generate")
	set(${PGO_TEST_OPTION} ON CACHE BOOL "Forced ON by the PGO training build" FORCE)
	_pgo_apply_stage_flags(generate)
	message(STATUS "ENABLE_PGO: instrumented build, profile ${PGO_PROFILE_DIR} (this tree and static dependencies)")
	return()
endif()

set(${PGO_TEST_OPTION} OFF CACHE BOOL "Disabled in the PGO optimized build" FORCE)
message(STATUS "ENABLE_PGO: training in ${PGO_INSTR_DIR} before this directory configures")

# Drop cmake_install.cmake files an earlier ENABLE_PGO wrote into the source tree.
function(_pgo_remove_stray_install_scripts dir)
	get_property(subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
	foreach(sub IN LISTS subs)
		if(EXISTS "${sub}/cmake_install.cmake")
			file(REMOVE "${sub}/cmake_install.cmake")
		endif()
		_pgo_remove_stray_install_scripts("${sub}")
	endforeach()
endfunction()

function(pgo_attach_training)
	_pgo_remove_stray_install_scripts("${CMAKE_SOURCE_DIR}")

	set(_pgo_profdata "")
	if(_pgo_kind STREQUAL "CLANG" OR _pgo_kind STREQUAL "CLANG_CL")
		get_filename_component(_pgo_bindir "${CMAKE_CXX_COMPILER}" DIRECTORY)
		find_program(PGO_LLVM_PROFDATA NAMES llvm-profdata llvm-profdata.exe HINTS "${_pgo_bindir}")
		if(NOT PGO_LLVM_PROFDATA AND APPLE)
			execute_process(
				COMMAND xcrun --find llvm-profdata
				OUTPUT_VARIABLE PGO_LLVM_PROFDATA
				OUTPUT_STRIP_TRAILING_WHITESPACE
				ERROR_QUIET
			)
		endif()
		if(NOT PGO_LLVM_PROFDATA OR NOT EXISTS "${PGO_LLVM_PROFDATA}")
			file(GLOB _pgo_profdata_cands
				"${_pgo_bindir}/llvm-profdata"
				"${_pgo_bindir}/llvm-profdata-[0-9]*"
				"${_pgo_bindir}/llvm-profdata.exe"
			)
			if(_pgo_profdata_cands)
				list(GET _pgo_profdata_cands 0 PGO_LLVM_PROFDATA)
			endif()
		endif()
		if(NOT PGO_LLVM_PROFDATA OR NOT EXISTS "${PGO_LLVM_PROFDATA}")
			message(FATAL_ERROR "ENABLE_PGO: llvm-profdata not found. It ships with the Clang toolchain (xcrun llvm-profdata on Apple Clang).")
		endif()
		set(_pgo_profdata "${PGO_LLVM_PROFDATA}")
	endif()

	set(INSTR_DIR "${PGO_INSTR_DIR}")
	set(SOURCE_DIR "${CMAKE_SOURCE_DIR}")
	set(BUILD_DIR "${CMAKE_BINARY_DIR}")
	set(PROFILE_DIR "${PGO_PROFILE_DIR}")
	if(PGO_TEST_DIR)
		set(TEST_DIR "${PGO_TEST_DIR}")
	else()
		set(TEST_DIR "${PGO_INSTR_DIR}/test")
	endif()
	set(TEST_OPTION "${PGO_TEST_OPTION}")
	set(KIND "${_pgo_kind}")
	set(GENERATOR "${CMAKE_GENERATOR}")
	set(GEN_PLATFORM "${CMAKE_GENERATOR_PLATFORM}")
	set(GEN_TOOLSET "${CMAKE_GENERATOR_TOOLSET}")
	set(TOOLCHAIN "${CMAKE_TOOLCHAIN_FILE}")
	set(C_COMPILER "${CMAKE_C_COMPILER}")
	set(CXX_COMPILER "${CMAKE_CXX_COMPILER}")
	set(CTEST "${CMAKE_CTEST_COMMAND}")
	set(PROFDATA "${_pgo_profdata}")
	if(CMAKE_CONFIGURATION_TYPES)
		set(MULTI 1)
	else()
		set(MULTI 0)
	endif()
	set(BUILD_TYPE "${CMAKE_BUILD_TYPE}")
	include(ProcessorCount)
	ProcessorCount(JOBS)
	if(NOT JOBS OR JOBS EQUAL 0)
		set(JOBS 1)
	endif()
	# Compiler and linker flags are CMAKE_* but they are part of the build the
	# user asked for, same as a project option. The rest of CMAKE_* is CMake's
	# own bookkeeping and must be rediscovered in the child.
	set(_pgo_flag_cache
		CMAKE_C_FLAGS
		CMAKE_CXX_FLAGS
		CMAKE_C_FLAGS_DEBUG
		CMAKE_C_FLAGS_MINSIZEREL
		CMAKE_C_FLAGS_RELEASE
		CMAKE_C_FLAGS_RELWITHDEBINFO
		CMAKE_CXX_FLAGS_DEBUG
		CMAKE_CXX_FLAGS_MINSIZEREL
		CMAKE_CXX_FLAGS_RELEASE
		CMAKE_CXX_FLAGS_RELWITHDEBINFO
		CMAKE_EXE_LINKER_FLAGS
		CMAKE_SHARED_LINKER_FLAGS
		CMAKE_MODULE_LINKER_FLAGS
		CMAKE_STATIC_LINKER_FLAGS
		CMAKE_EXE_LINKER_FLAGS_DEBUG
		CMAKE_EXE_LINKER_FLAGS_MINSIZEREL
		CMAKE_EXE_LINKER_FLAGS_RELEASE
		CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO
		CMAKE_SHARED_LINKER_FLAGS_DEBUG
		CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL
		CMAKE_SHARED_LINKER_FLAGS_RELEASE
		CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO
		CMAKE_MODULE_LINKER_FLAGS_DEBUG
		CMAKE_MODULE_LINKER_FLAGS_MINSIZEREL
		CMAKE_MODULE_LINKER_FLAGS_RELEASE
		CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO
		CMAKE_AR
		CMAKE_RANLIB
		CMAKE_NM
		CMAKE_LINKER
		CMAKE_MSVC_RUNTIME_LIBRARY
		CMAKE_C_STANDARD
		CMAKE_CXX_STANDARD
		CMAKE_POSITION_INDEPENDENT_CODE
		CMAKE_INSTALL_PREFIX
		CMAKE_PREFIX_PATH
		CMAKE_FIND_ROOT_PATH
		CMAKE_MODULE_PATH
		CMAKE_PROGRAM_PATH
		CMAKE_LIBRARY_PATH
		CMAKE_INCLUDE_PATH
		CMAKE_FRAMEWORK_PATH
		CMAKE_OSX_ARCHITECTURES
		CMAKE_OSX_DEPLOYMENT_TARGET
		CMAKE_SYSROOT
		CMAKE_FIND_PACKAGE_PREFER_CONFIG
	)
	set(_pgo_never_forward
		PGO_STAGE
		PGO_PROFILE_DIR
		PGO_INSTR_DIR
		PGO_TEST_DIR
		PGO_FORWARD_CACHE
		CMAKE_HOME_DIRECTORY
		CMAKE_CACHEFILE_DIR
		ENABLE_PGO
		${PGO_TEST_OPTION}
	)
	get_cmake_property(_pgo_cache_names CACHE_VARIABLES)
	set(FORWARD "")
	set(_pgo_seen "")
	foreach(name IN LISTS _pgo_cache_names)
		set(_pgo_skip FALSE)
		if(name IN_LIST _pgo_never_forward)
			set(_pgo_skip TRUE)
		endif()
		if(NOT _pgo_skip)
			get_property(_pgo_cache_type CACHE "${name}" PROPERTY TYPE)
			if(_pgo_cache_type MATCHES "INTERNAL|STATIC")
				set(_pgo_skip TRUE)
			endif()
		endif()
		if(NOT _pgo_skip)
			set(_pgo_take FALSE)
			if(name IN_LIST _pgo_flag_cache OR name IN_LIST PGO_FORWARD_CACHE)
				set(_pgo_take TRUE)
			elseif(NOT name MATCHES "^(CMAKE_|_)")
				set(_pgo_take TRUE)
			endif()
			if(NOT _pgo_take)
				set(_pgo_skip TRUE)
			endif()
		endif()
		if(NOT _pgo_skip)
			get_property(_val CACHE "${name}" PROPERTY VALUE)
			if(name STREQUAL "CMAKE_C_FLAGS" OR name STREQUAL "CMAKE_CXX_FLAGS"
				OR name STREQUAL "CMAKE_EXE_LINKER_FLAGS"
				OR name STREQUAL "CMAKE_SHARED_LINKER_FLAGS"
				OR name STREQUAL "CMAKE_MODULE_LINKER_FLAGS")
				_pgo_strip_profile_flags("${_val}" _val)
			endif()
			string(FIND "${_val}" "${CMAKE_BINARY_DIR}" _pgo_at)
			if(NOT _pgo_at EQUAL -1 AND NOT name IN_LIST PGO_FORWARD_CACHE)
				string(APPEND FORWARD "message(STATUS \"ENABLE_PGO: skipped ${name} (points at this build directory)\")\n")
				set(_pgo_skip TRUE)
			endif()
		endif()
		if(NOT _pgo_skip)
			string(REPLACE ";" "\\;" _val "${_val}")
			string(REPLACE "\\" "\\\\" _val "${_val}")
			string(REPLACE "\"" "\\\"" _val "${_val}")
			string(REPLACE "\$" "\\\$" _val "${_val}")
			string(REPLACE "\n" " " _val "${_val}")
			string(APPEND FORWARD "list(APPEND _cfg \"-D${name}=${_val}\")\n")
			string(APPEND FORWARD "message(STATUS \"ENABLE_PGO: forward ${name}=${_val}\")\n")
			list(APPEND _pgo_seen "${name}")
		endif()
	endforeach()
	# CMAKE_* is not forwarded above. A compiler launcher is one of the few that
	# must be: without it the instrumented tree and the optimized rebuild miss
	# ccache/sccache. An empty value is left unset so the child can still take
	# the environment. The environment is the fallback when the variable was
	# never copied into this cache.
	foreach(name IN ITEMS CMAKE_C_COMPILER_LAUNCHER CMAKE_CXX_COMPILER_LAUNCHER)
		set(_val "${${name}}")
		if(_val STREQUAL "" AND DEFINED ENV{${name}} AND NOT "$ENV{${name}}" STREQUAL "")
			set(_val "$ENV{${name}}")
		endif()
		if(_val STREQUAL "")
			continue()
		endif()
		string(REPLACE ";" "\\;" _val "${_val}")
		string(REPLACE "\\" "\\\\" _val "${_val}")
		string(REPLACE "\"" "\\\"" _val "${_val}")
		string(REPLACE "\$" "\\\$" _val "${_val}")
		string(REPLACE "\n" " " _val "${_val}")
		string(APPEND FORWARD "list(APPEND _cfg \"-D${name}=${_val}\")\n")
		string(APPEND FORWARD "message(STATUS \"ENABLE_PGO: forward ${name}=${_val}\")\n")
		list(APPEND _pgo_seen "${name}")
	endforeach()
	if(NOT _pgo_seen)
		string(APPEND FORWARD "message(STATUS \"ENABLE_PGO: forward (none)\")\n")
	endif()

	string(CONFIGURE [=[
set(KIND "@KIND@")
set(_cfg "@CMAKE_COMMAND@" -S "@SOURCE_DIR@" -B "@INSTR_DIR@" -G "@GENERATOR@")
if(NOT "@GEN_PLATFORM@" STREQUAL "")
	list(APPEND _cfg -A "@GEN_PLATFORM@")
endif()
if(NOT "@GEN_TOOLSET@" STREQUAL "")
	list(APPEND _cfg -T "@GEN_TOOLSET@")
endif()
if(NOT "@TOOLCHAIN@" STREQUAL "")
	list(APPEND _cfg "-DCMAKE_TOOLCHAIN_FILE=@TOOLCHAIN@")
endif()
if(NOT @MULTI@)
	list(APPEND _cfg "-DCMAKE_BUILD_TYPE=@BUILD_TYPE@")
endif()
list(APPEND _cfg
	"-DCMAKE_C_COMPILER=@C_COMPILER@"
	"-DCMAKE_CXX_COMPILER=@CXX_COMPILER@"
	"-DENABLE_PGO=ON"
	"-DPGO_STAGE=generate"
	"-DPGO_PROFILE_DIR=@PROFILE_DIR@"
	"-DPGO_TEST_OPTION=@TEST_OPTION@"
	"-D@TEST_OPTION@=ON"
)
message(STATUS "ENABLE_PGO: configuring instrumented build @INSTR_DIR@")
@FORWARD@
execute_process(COMMAND ${_cfg} RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "ENABLE_PGO: instrumented configure failed (${_rc})")
endif()
set(_build "@CMAKE_COMMAND@" --build "@INSTR_DIR@" --parallel @JOBS@)
if(@MULTI@ AND NOT CONFIG STREQUAL "")
	list(APPEND _build --config "${CONFIG}")
endif()
message(STATUS "ENABLE_PGO: building instrumented tree (-j@JOBS@)")
set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} "@JOBS@")
execute_process(COMMAND ${_build} RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "ENABLE_PGO: instrumented build failed (${_rc})")
endif()
if(NOT EXISTS "@TEST_DIR@/CTestTestfile.cmake")
	message(FATAL_ERROR "ENABLE_PGO: no tests in @TEST_DIR@. Set PGO_TEST_OPTION to the cache option that enables them.")
endif()
set(_ctest "@CTEST@" --output-on-failure)
if(@MULTI@ AND NOT CONFIG STREQUAL "")
	list(APPEND _ctest -C "${CONFIG}")
endif()
set(_dll_dirs)
foreach(_rel IN ITEMS
		"lib" "lib64" "bin"
		"buildmaster/install/bin"
		"buildmaster/install/lib"
		"buildmaster/install/lib64"
		"thirdparty/buildmaster/install/bin"
		"thirdparty/buildmaster/install/lib"
		"thirdparty/buildmaster/install/lib64")
	if(@MULTI@ AND NOT CONFIG STREQUAL "" AND EXISTS "@INSTR_DIR@/${_rel}/${CONFIG}")
		list(APPEND _dll_dirs "@INSTR_DIR@/${_rel}/${CONFIG}")
	endif()
	if(EXISTS "@INSTR_DIR@/${_rel}")
		list(APPEND _dll_dirs "@INSTR_DIR@/${_rel}")
	endif()
endforeach()
if(_dll_dirs)
	if(WIN32)
		list(JOIN _dll_dirs ";" _dll_path)
		set(ENV{PATH} "${_dll_path};$ENV{PATH}")
		message(STATUS "ENABLE_PGO: PATH += ${_dll_path}")
	else()
		list(JOIN _dll_dirs ":" _dll_path)
		set(ENV{LD_LIBRARY_PATH} "${_dll_path};$ENV{LD_LIBRARY_PATH}")
		set(ENV{DYLD_LIBRARY_PATH} "${_dll_path};$ENV{DYLD_LIBRARY_PATH}")
		message(STATUS "ENABLE_PGO: LD_LIBRARY_PATH += ${_dll_path}")
	endif()
endif()
message(STATUS "ENABLE_PGO: ctest in @TEST_DIR@")
execute_process(COMMAND ${_ctest} WORKING_DIRECTORY "@TEST_DIR@" RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
	message(FATAL_ERROR "ENABLE_PGO: ctest failed (${_rc})")
endif()
if(KIND STREQUAL "CLANG" OR KIND STREQUAL "CLANG_CL")
	file(GLOB _raws "@PROFILE_DIR@/*.profraw")
	if(NOT _raws)
		message(FATAL_ERROR "ENABLE_PGO: ctest wrote no .profraw files in @PROFILE_DIR@")
	endif()
	execute_process(
		COMMAND "@PROFDATA@" merge -sparse "-output=@PROFILE_DIR@/default.profdata" ${_raws}
		RESULT_VARIABLE _rc
	)
	if(NOT _rc EQUAL 0)
		message(FATAL_ERROR "ENABLE_PGO: llvm-profdata merge failed (${_rc})")
	endif()
endif()
if(KIND STREQUAL "MSVC")
	file(GLOB_RECURSE _profs "@INSTR_DIR@/*.pgd" "@INSTR_DIR@/*.pgc")
	foreach(_src IN LISTS _profs)
		file(RELATIVE_PATH _rel "@INSTR_DIR@" "${_src}")
		get_filename_component(_dst_dir "@BUILD_DIR@/${_rel}" DIRECTORY)
		file(MAKE_DIRECTORY "${_dst_dir}")
		file(COPY "${_src}" DESTINATION "${_dst_dir}")
	endforeach()
endif()
]=] _pgo_script @ONLY)
	file(WRITE "${CMAKE_BINARY_DIR}/pgo-run.cmake" "${_pgo_script}")

	set(_pgo_config "${CMAKE_BUILD_TYPE}")
	if(_pgo_config STREQUAL "")
		set(_pgo_config "Release")
	endif()
	message(STATUS "ENABLE_PGO: instrument, ctest, then this directory configures against the profile")
	execute_process(
		COMMAND ${CMAKE_COMMAND} -DCONFIG=${_pgo_config} -P "${CMAKE_BINARY_DIR}/pgo-run.cmake"
		RESULT_VARIABLE _pgo_rc
	)
	if(NOT _pgo_rc EQUAL 0)
		message(FATAL_ERROR "ENABLE_PGO: training failed (${_pgo_rc})")
	endif()
endfunction()

# Training is finished. The profile file exists. Flags applied here are what
# BuildMaster bakes into static dependencies, and what this directory compiles
# with. thirdparty is added by the caller after this file returns.
pgo_attach_training()
_pgo_apply_stage_flags(use)
message(STATUS "ENABLE_PGO: profile ready in ${PGO_PROFILE_DIR}. This directory and its static dependencies use it")
