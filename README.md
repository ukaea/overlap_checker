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
apt-get install cmake libocct-foundation-dev libocct-data-exchange-dev
```

Note, using Ubuntu 21.04 is recommended to get OpenCascade 7.5, Ubuntu
20.04 LTS is known to fail due to the library being too old.

Under ArchLinux the above dependencies would be installed via:

```shell
pacman -S cmake opencascade
```

## Cloning

When fetching the source code please ensure that sub-modules are also
cloned, e.g. via:

```shell
git clone --recurse-submodules https://github.com/ukaea/overlap_checker.git
```

## Git Large File Storage (LFS)

Testing this code involves a number of geometry files that aren't
suitable for tracking directly in Git. We're using Git LFS instead to
track these so this must be installed first, if you want to run
regression tests.  See https://git-lfs.github.com/ for details.

Under Ubuntu 22.04, git-lfs can be installed via:

```shell
apt-get install git-lfs
git lfs install
```

Then to get the test data:

```shell
git lfs pull
```


## Building

Next we set up a build directory, compile the code, and run the unit
tests via:

```shell
# set up and enter a build directory
mkdir build; cd build
cmake ..

# compile the code
make -j6

# run tests
ctest

# install executables
make install
```

## *Experimental* Conda integration for Blueprint

This package is designed to function within the Blueprint ecosystem
which predominantly uses Conda for package management. The following
recipe should work locally as well as within a `condaforge/miniforge3`
Docker container:

```shell
# ensure system level tools are installed
apt-get update && apt-get install libgl1 cmake g++

# create a conda environment and activate it
conda create -n bluemira python
conda activate bluemira

# install dependencies
conda install ninja meson occt fmt spdlog cli11 doctest

# set up and enter build directly
CXX=/usr/bin/g++ meson setup build \
  -Dconda_prefix=$CONDA_PREFIX -Duse_conan=false

cd build

# compile the code
meson compile

# run tests
meson test -v
```

Using the GCC C++ compiler installed within Conda causes the OpenGL
libraries (depended on by OpenCascade) to fail to link, hence using an
externally installed version.

The `use_conan` parameter controls whether dependencies are fetched
via Conan, or are assumed to exist within the system. Allowing
advanced users to install them, and use the following during the setup
step:

```shell
meson setup build -Duse_conan=false
```

## Notes

 * Some editors use `clangd` to provide autocompletions and other
   helpful tools, using the Clang compiler can help with this.

I need to use the following to accomplish this:

```shell
export CC=clang CXX=clang++
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

A demo workflow is available in `tests/demo_workflow.sh`, and can be
executed on a simple demo geometry as:

```shell
bash tests/demo_workflow.sh ../data/test_geometry.step
```

All output will be written into the current directory, a prefixed by
`test_geometry`.  The workflow is similar to the following:

```shell
# linearise all the shapes in the STEP file so they can be indexed consistantly by subsequent tools
step_to_brep ../data/test_geometry.step test_geometry.brep > test_geometry-geometry.csv

# perform overlap checking
overlap_checker test_geometry.brep > test_geometry-overlaps.csv

# collect overlapping solids and write to common.brep
grep overlap test_geometry-overlaps.csv | overlap_collecter test_geometry.brep test_geometry-common.brep

# perform imprinting
imprint_solids test_geometry.brep test_geometry-imprinted.brep < test_geometry-overlaps.csv

# merging of imprinted solids
merge_solids test_geometry-imprinted.brep test_geometry-merged.brep
```

# Terminology

There are a few terms that are used a lot and I'll try to describe
them here:

## Imprinting

This ensures shapes that touch or intersect in any way both have
equivalent features at the same place. For example if a rectangular
face encompasses another smaller rectangular face, then that smaller
face will be inscribed/imprinted into the larger face by removing the
overlapping area, then adding 4 vertices and edges, and a face to the
larger face, and stitching up the result.

## Merging

This ensures that "topologically identical" features (e.g. vertices in
the same place) are recorded as being the same. This ensures that
subsequent processing steps are able to easily identify these shared
features. For example, when meshing, we'd want mesh vertices along a
shared edge to be the same and meshers will ensure this if they know a
single edge is used rather than two separate edges that happen to be
at the same location.

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

Note that the `brep_flatten` tool might be useful if you already have
a BREP file that you got from somewhere else.

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

## `merge_solids`

This tool glues shared parts of solids together. It works from
vertices upto compound solid, merging any shared edges/faces as
appropriate. This is an intermediate step in our neutronics workflow
and aims to produce output compatible with [occ_faceter][].

[pyvenv]: https://docs.python.org/3/tutorial/venv.html
[occ_faceter]: https://github.com/makeclean/occ_faceter/


# File formats

The geometry data is stored in [BREP][brep_format] format with some constraints
designed to simplify subsequent processing tasks. Our workflow is
centered around [solids][occt_topological_types] and their materials.
Each solid has a defined material, and it's important to be able to
maintain this link during the processing steps. During import, e.g.
`step_to_brep`, the hierchial structure is flattened to a list of
solids, represented in OCCT and the BREP file as a COMPOUND. A CSV
file describing material information is also written that uses the
same ordering.

Subsequent tools that modify the shapes, e.g. `imprint_solids`,
maintain the top-level order. For example if there is an intersection
between shapes 3 and 4, the overlapping volume will be added to the
larger shape (e.g. shape 4). Hence, after imprinting shape 3 will be
slightly smaller while shape 4 will now be a COMPSOLID containing two
SOLIDS, one solid being this small intersecting volume and a copy of
shape 4 that has been modified to remove this intersection.

As a visual representation, our BREP format has a COMPOUND as the
top-level shape with either SOLIDS or COMPSOLIDs inside. For example,
after the above imprinting we might have:

```
 * COMPOUND
   * SOLID
   * SOLID
   * SOLID
   * SOLID
   * COMPSOLID
     * SOLID
     * SOLID
   * SOLID
```

note that numbering is zero based.

[brep_format]: https://dev.opencascade.org/doc/occt-6.7.0/overview/html/occt_brep_format.html
[occt_topological_types]: https://dev.opencascade.org/doc/overview/html/occt_user_guides__modeling_data.html#occt_modat_5_2_1


# Coverage

Currently a few commands I found useful to run:

```shell
# configure with coverage enabled
env CXX=clang++ cmake build . -B build \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON

cd build

# run program
./merge_solids ../data/paramak_reactor.brep out.brep

# convert data into a format usable by llvm-cov
llvm-profdata merge -o testcov.profdata default.profraw

# generate html coverage report
llvm-cov show -output-dir=report -format=html ./merge_solids -instr-profile=testcov.profdata
```

can use `LLVM_PROFILE_FILE=paramak_reactor-merge.profraw` to output
raw profile to another file. see [cov1][1] and [cov2][2] for more
details

[cov1]: https://alastairs-place.net/blog/2016/05/20/code-coverage-from-the-command-line-with-clang/
[cov2]: https://clang.llvm.org/docs/SourceBasedCodeCoverage.html
