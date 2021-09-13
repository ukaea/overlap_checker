This repository contains code to validate and preprocess CAD files
(e.g. STEP files, as produced by FreeCad) for integration into
modelling workflows, for example simulation in OpenMC.


# Building

Building currently requires Python 3 and a recent version of
OpenCascade to be installed on your system. Under ArchLinux I can
install opencascade and :

```shell
pacman -S python opencascade
```

I'll fill in instructions for other distributions / OSs as I get them
working.  Suggestions welcome!

After this, I use Python to instal the required build tools, then use
conan to install the required dependencies.

```shell
# install build tools, maybe in a seperate venv
pip install -U conan meson ninja

# if you want to use clangd for IDE integration, setting these might help
export CC=clang CXX=clang++

# pre-built packages don't exist for me, so I do
conan install --build=fmt fmt/8.0.1@
conan install --build=spdlog spdlog/1.9.2@
conan install --build=cli11 cli11/2.0.0@
conan install --build=doctest doctest/2.4.6@
```

Now that we've got the required dependencies set up, we use meson to
set up the build files for ninja, and finally use ninja to build the
code:

```shell
# set up build directory
meson setup build

# build code
cd build
ninja

# run tests
./test_runner
```

Note that meson makes `build/compile_commands.json` which can be
pulled in by your IDE to provide type information and autocompletions.


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


For example, to check `input.step` for overlapping solids and writing
out intersections to `common.brep`, you can do:

```shell
step_to_brep input.step input.brep > shapes.csv
overlap_checker -j8 input.brep > overlaps.csv
grep overlap overlaps.csv | overlap_collecter input.brep common.brep
```
