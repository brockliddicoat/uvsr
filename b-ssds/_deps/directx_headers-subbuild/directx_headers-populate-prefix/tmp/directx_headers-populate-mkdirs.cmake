# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/brock/Documents/uvsr/work/screen-space-directional-shadow-research-port/b-ssds/_deps/directx_headers-src")
  file(MAKE_DIRECTORY "C:/Users/brock/Documents/uvsr/work/screen-space-directional-shadow-research-port/b-ssds/_deps/directx_headers-src")
endif()
file(MAKE_DIRECTORY
  "C:/Users/brock/Documents/uvsr/work/screen-space-directional-shadow-research-port/b-ssds/_deps/directx_headers-build"
  "C:/Users/brock/Documents/uvsr/work/screen-space-directional-shadow-research-port/b-ssds/_deps/directx_headers-subbuild/directx_headers-populate-prefix"
  "C:/Users/brock/Documents/uvsr/work/screen-space-directional-shadow-research-port/b-ssds/_deps/directx_headers-subbuild/directx_headers-populate-prefix/tmp"
  "C:/Users/brock/Documents/uvsr/work/screen-space-directional-shadow-research-port/b-ssds/_deps/directx_headers-subbuild/directx_headers-populate-prefix/src/directx_headers-populate-stamp"
  "C:/Users/brock/Documents/uvsr/work/screen-space-directional-shadow-research-port/b-ssds/_deps/directx_headers-subbuild/directx_headers-populate-prefix/src"
  "C:/Users/brock/Documents/uvsr/work/screen-space-directional-shadow-research-port/b-ssds/_deps/directx_headers-subbuild/directx_headers-populate-prefix/src/directx_headers-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/brock/Documents/uvsr/work/screen-space-directional-shadow-research-port/b-ssds/_deps/directx_headers-subbuild/directx_headers-populate-prefix/src/directx_headers-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/brock/Documents/uvsr/work/screen-space-directional-shadow-research-port/b-ssds/_deps/directx_headers-subbuild/directx_headers-populate-prefix/src/directx_headers-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
