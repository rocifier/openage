add_sources(libopenage
	directory.cpp
	mounted_directories.cpp
	fslike.cpp
	native.cpp
)

pxdgen(
	fslike.h
	python.h
)

if(FVD_WITH_PYTHON_ADAPTERS)
	add_sources(libopenage python.cpp)
endif()
