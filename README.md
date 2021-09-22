This project contains a variety of small tools to validate and
preprocess CAD files (e.g. STEP files, as produced by FreeCad) for
integration into modelling workflows, for example simulation in
OpenMC.

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

This functionality can be used to allow Conda to fetch the
dependencies then we pull them into Meson via:

```shell
conda install ninja meson fmt spdlog cli11 doctest

env CXXFLAGS=-I"$CONDA_PREFIX/include/opencascade" LDFLAGS=-L"$CONDA_PREFIX/lib" \
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

Within the `condaforge/miniforge3` Docker container, the following
recipe can be used:

```shell
# conda compiled OCCT assumes opengl is preinstalled
apt-get update && apt-get install -y libgl1 cmake g++

# install into base environment
conda install ninja meson occt fmt spdlog cli11 doctest

# point compiler at conda installed dependencies
export CXXFLAGS=-I/opt/conda/include/opencascade
export LDFLAGS=-L/opt/conda/lib"

# configure Ninja and build
meson setup build -Duse_conan=false
ninja -C build
```

# Running

Three small tools are provided that can be used in different
workflows, they are:

 * `step_to_brep` extracts the solid shapes out of a STEP file and
   write them to a BREP file, along with CSV output of labels,
   material, and colour information.
 * `overlap_checker` performs pairwise checks on all solids, writing
   out a CSV file listing when they touch or overlap.
 * `overlap_collecter` collect intersections between solids and write
   out to BREP file.
 * `imprint_solids` removes any overlaps from solids, modifying
   veticies, edges and faces as appropriate.

For example, to run the tools on the included `test_geometry.step`
file you can do:

```shell
# linearise all the shapes in the STEP file so they can be indexed consistantly by subsequent tools
step_to_brep ../data/test_geometry.step test_geometry.brep > test_geometry.csv

# perform overlap checking using 8 cores
overlap_checker -j8 test_geometry.brep > overlaps.csv

# collect overlapping solids and write to common.brep
grep overlap overlaps.csv | overlap_collecter test_geometry.brep common.brep

# perform imprinting
imprint_solids test_geometry.brep imprinted.brep < overlaps.csv
```

# Tool descriptions

Note that all programs support passing the standard Unix `--help` (and
the terse `-h` variant) to get information about supported command
line arguments. For example, running:

```shell
overlap_checker -h
```

will print out information about how to specify the amount of
parallelisation to exploit, tolerances on bounding boxes, volumes and
clearances.

## `step_to_brep`

This tool is the starting point of this toolkit. It recursively
collects all solid shapes out of a STEP file and writes them out to a
BREP file. It also tries to collect color and material information
from the input file and outputs them to standard out, under the
expectation that this is directed to a file so it can be used by other
tools.

## `overlap_checker`

This tool performs pairwise comparisons between all nearby solids
outputting a CSV file for all cases where they touch (i.e. share a
vertex or edge) or overlap. The user can specify a tolerance that is
used when comparing shapes allowing small modelling/floating point
errors (i.e. <0.001mm, see command line) to be considered as
specifying the "same" thing.

When two solids overlap more than trivially (more than 1%, see command
line for control over this) then this is determined to be an error
with the geometry. The resulting non-zero exit status is designed to
help CI pipelines.

Note that this is a computationally expensive operation so is
parallelised, you can specify the number of threads to use with the
`-j` parameter.

## `overlap_collecter`

This tool collects the overlapping area between the shapes specified
by a CSV file. The overlapping volumes are written to a BREP file
under the expectation that this is read into a GUI along side the
original STEP file so highlight where the overlaps occurred.

## `imprint_solids`

This tool tries to clean up any overlapping volumes from solids. This
uses the same tolerance options as `overlap_checker`, and hence
vertices/edges of "touching" shapes might move due nearby shapes being
within this tolerance.

[pyvenv]: https://docs.python.org/3/tutorial/venv.html
