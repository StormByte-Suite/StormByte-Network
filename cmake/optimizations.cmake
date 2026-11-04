option(ENABLE_LTO "Link-time optimization. Not tied to the build type. Combines with ENABLE_PGO." ON)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION "${ENABLE_LTO}")
foreach(_lto_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_${_lto_cfg} "${ENABLE_LTO}")
endforeach()
unset(_lto_cfg)
