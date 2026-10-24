# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/brock/Documents/uvsr/work/heitz-ratio-estimator-repair/b-final/_deps/dxc-src")
  file(MAKE_DIRECTORY "C:/Users/brock/Documents/uvsr/work/heitz-ratio-estimator-repair/b-final/_deps/dxc-src")
endif()
file(MAKE_DIRECTORY
  "C:/Users/brock/Documents/uvsr/work/heitz-ratio-estimator-repair/b-final/_deps/dxc-build"
  "C:/Users/brock/Documents/uvsr/work/heitz-ratio-estimator-repair/b-final/_deps/dxc-subbuild/dxc-populate-prefix"
  "C:/Users/brock/Documents/uvsr/work/heitz-ratio-estimator-repair/b-final/_deps/dxc-subbuild/dxc-populate-prefix/tmp"
  "C:/Users/brock/Documents/uvsr/work/heitz-ratio-estimator-repair/b-final/_deps/dxc-subbuild/dxc-populate-prefix/src/dxc-populate-stamp"
  "C:/Users/brock/Documents/uvsr/work/heitz-ratio-estimator-repair/b-final/_deps/dxc-subbuild/dxc-populate-prefix/src"
  "C:/Users/brock/Documents/uvsr/work/heitz-ratio-estimator-repair/b-final/_deps/dxc-subbuild/dxc-populate-prefix/src/dxc-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/brock/Documents/uvsr/work/heitz-ratio-estimator-repair/b-final/_deps/dxc-subbuild/dxc-populate-prefix/src/dxc-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/brock/Documents/uvsr/work/heitz-ratio-estimator-repair/b-final/_deps/dxc-subbuild/dxc-populate-prefix/src/dxc-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
