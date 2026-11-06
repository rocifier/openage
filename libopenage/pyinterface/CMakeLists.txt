if(FVD_WITH_PYTHON_ADAPTERS)
add_sources(libopenage
	exctranslate.cpp
	exctranslate_tests.cpp
	defs.cpp
	functional.cpp
	hacks.cpp
	pyexception.cpp
	pyobject.cpp
	pyobject_tests.cpp
	setup.cpp
)

endif()

pxdgen(
	exctranslate.h
	exctranslate_tests.h
	defs.h
	functional.h
	hacks.h
	pyexception.h
	pyobject.h
	setup.h
)
