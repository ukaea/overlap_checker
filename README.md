This project contains code to validate and preprocess CAD files (e.g.
STEP files, as produced by FreeCad) for integration into modelling
workflows, for example simulation in OpenMC.


# Building

Building currently requires a recent version of OpenCascade's headers
and libraries to be installed on your system, along with CMake and
Python.

Under Debian/Ubuntu these can be installed by running:

```shell
apt-get install cmake python3-pip libocct-foundation-dev libocct-data-exchange-dev
```

Note, using Ubuntu 21.04 is recommended to get OpenCascade 7.5, Ubuntu
20.04 LTS is known to fail due to the library being too old.

Under ArchLinux the dependencies would be installed via:

```shell
pacman -S cmake python opencascade
```

Once these have been installed, we can use Python to fetch our build
tools. Note, I'd suggest doing this in a [virtual environment][pyvenv]
to keep them tidy.

```shell
# install build tools, maybe in a seperate venv
pip install -U conan meson ninja
```

Meson is used for build configuration, calling out to Conan for C++
package management. Ninja is used as a modern replacement for make.

```shell
# set up build directory
CXXFLAGS=-I/usr/include/opencascade meson setup build

# build code
ninja -C build

# run tests
build/test_runner
```

While developing, just running `ninja` should be enough. It should
automatically run Meson if it needs to.

## Bring-your-own package manager

If you don't want to use Conan to install the above dependencies you
can use do it yourself, or use your operating system's one if it has
them, and disable conan passing `-Duse_conan=false` to Meson by
running:

```shell
CXXFLAGS=-I/usr/include/opencascade meson setup build -Duse_conan=false
```

This can be used to allow Conda to fetch the dependencies, like:

```shell
conda install ninja meson fmt spdlog cli11 doctest

env CXXFLAGS=-I/opt/conda/include/opencascade LDFLAGS=-L/opt/conda/lib \
  meson setup build -Duse_conan=false
```

## Notes

 * Some editors use `clangd` to provide autocompletions and other
   helpful tools, using the Clang compiler can help with this.
 * Prebuilt Conan binaries are only available for some systems, so you
   might need to explicitly build them.

These can be accomplished with:

```shell
export CC=clang CXX=clang++

conan install --build=fmt fmt/8.0.1@
conan install --build=spdlog spdlog/1.9.2@
conan install --build=cli11 cli11/2.0.0@
conan install --build=doctest doctest/2.4.6@
```

# Running

Three small tools are provided that can be used in different
workflows, they are:

 * `step_to_brep` extracts the solid shapes out of a STEP file and
   write them to a BREP file, along with CSV output of labels,
   material, and colour information.
 * `overlap_checker` performs pairwise checks on the above output,
   writing out a CSV file listing when solids touch or overlap.
 * `overlap_collecter` collect intersections between solids and write
   out to BREP file.

For example, to check the included `test_geometry.step` for
overlapping solids and writing out intersections to `common.brep`, you
can do:

```shell
step_to_brep ../data/test_geometry.step solids.brep > shapes.csv
overlap_checker -j8 solids.brep > overlaps.csv
grep overlap overlaps.csv | overlap_collecter solids.brep common.brep
```

[pyvenv]: https://docs.python.org/3/tutorial/venv.html
