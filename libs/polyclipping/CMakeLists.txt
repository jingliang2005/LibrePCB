
#find_library(CLIPPER_LIBRARIES_RELEASE polyclipping)

# Export library
add_library(polyclipping STATIC clipper.cpp clipper.hpp)

# Alias to namespaced variant
add_library(Polyclipping::Polyclipping ALIAS polyclipping)
