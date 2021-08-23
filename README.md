# building

I currently require Python 3 and a recent version of OpenCascade to be
installed on the system, under ArchLinux I can just do:

```shell
pacman -S python opencascade
```

Currently this is just exploratory code, so am not providing full
instructions for other distributions / OSs yet.

After this, I can do:

```shell
# install build tools, maybe in a seperate venv
pip install -U conan meson ninja

# if you want to use clangd for IDE integration, setting these might help
export CC=clang CXX=clang++

# packages don't exist for arch so need to do
conan install --build=spdlog spdlog/1.9.2@
conan install --build=fmt fmt/8.0.1@

# set up build directory
meson setup build

# the above also makes build/compile_commands.json to help your IDE find dependencies

# finally, build code
cd build
ninja

# run testcode
./parallel_preprocessor
```
