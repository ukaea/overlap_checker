#!/bin/bash

set -euxo pipefail

echo "$@"

pwd

env

ls -l ..

# assume we're running in the build directory which is a direct
# descendant of the source

./step_to_brep ../data/paramak_reactor.step paramak_reactor.brep

./merge_solids paramak_reactor.brep merged.brep

diff -u ../data/paramak_reactor-salome_glued.brep merged.brep
