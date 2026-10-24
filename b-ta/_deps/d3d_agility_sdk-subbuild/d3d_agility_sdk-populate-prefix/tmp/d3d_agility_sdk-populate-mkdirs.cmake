# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/brock/Documents/uvsr/work/temporal-accumulation-ui-repair/b-ta/_deps/d3d_agility_sdk-src")
  file(MAKE_DIRECTORY "C:/Users/brock/Documents/uvsr/work/temporal-accumulation-ui-repair/b-ta/_deps/d3d_agility_sdk-src")
endif()
file(MAKE_DIRECTORY
  "C:/Users/brock/Documents/uvsr/work/temporal-accumulation-ui-repair/b-ta/_deps/d3d_agility_sdk-build"
  "C:/Users/brock/Documents/uvsr/work/temporal-accumulation-ui-repair/b-ta/_deps/d3d_agility_sdk-subbuild/d3d_agility_sdk-populate-prefix"
  "C:/Users/brock/Documents/uvsr/work/temporal-accumulation-ui-repair/b-ta/_deps/d3d_agility_sdk-subbuild/d3d_agility_sdk-populate-prefix/tmp"
  "C:/Users/brock/Documents/uvsr/work/temporal-accumulation-ui-repair/b-ta/_deps/d3d_agility_sdk-subbuild/d3d_agility_sdk-populate-prefix/src/d3d_agility_sdk-populate-stamp"
  "C:/Users/brock/Documents/uvsr/work/temporal-accumulation-ui-repair/b-ta/_deps/d3d_agility_sdk-subbuild/d3d_agility_sdk-populate-prefix/src"
  "C:/Users/brock/Documents/uvsr/work/temporal-accumulation-ui-repair/b-ta/_deps/d3d_agility_sdk-subbuild/d3d_agility_sdk-populate-prefix/src/d3d_agility_sdk-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/brock/Documents/uvsr/work/temporal-accumulation-ui-repair/b-ta/_deps/d3d_agility_sdk-subbuild/d3d_agility_sdk-populate-prefix/src/d3d_agility_sdk-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/brock/Documents/uvsr/work/temporal-accumulation-ui-repair/b-ta/_deps/d3d_agility_sdk-subbuild/d3d_agility_sdk-populate-prefix/src/d3d_agility_sdk-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
