#!/bin/bash

set -euxo pipefail

# assume we're running in the build directory which is a direct
# descendant of the source

./merge_solids ../data/paramak_reactor.brep out.brep

diff -u ../data/paramak_reactor-salome_glued.brep out.brep
