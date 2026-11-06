add_sources(libopenage
	filelike.cpp
	native.cpp
)

pxdgen(
	filelike.h
	python.h
)

if(FVD_WITH_PYTHON_ADAPTERS)
	add_sources(libopenage python.cpp)
endif()
