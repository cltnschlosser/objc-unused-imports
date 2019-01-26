1. Make `clang-llvm` directory, this will be the root directory for everything related to this.
```bash
mkdir clang-llvm
cd clang-llvm
```

2. Tools needed:
	a. cmake
```bash
git clone https://gitlab.kitware.com/cmake/cmake.git cmake
```
	b. ninja
```bash
git clone https://github.com/martine/ninja.git ninja
```

3. llvm and clang
```bash
git clone http://llvm.org/git/llvm.git llvm

git clone http://llvm.org/git/clang.git llvm/tools/clang
```

4. Extra tools I needed to get llvm and clang compiling (some may not be needed)
```bash
git clone http://llvm.org/git/clang-tools-extra.git llvm/tools/clang/tools/extra
git clone http://llvm.org/git/lld.git llvm/tools/lld
git clone http://llvm.org/git/polly.git llvm/tools/polly
git clone http://llvm.org/git/libcxx.git llvm/projects/libcxx
git clone https://git.llvm.org/git/compiler-rt.git llvm/projects/compiler-rt
git clone http://llvm.org/git/libcxxabi.git llvm/projects/libcxxabi
git clone http://llvm.org/git/test-suite.git llvm/projects/test-suite
```

5. Installing ninja
```bash
cd clang-llvm/ninja
git checkout release
./bootstrap.py
sudo cp ninja /usr/local/bin/
```

6. Installing cmake
```bash
cd clang-llvm/cmake
git checkout next
./bootstrap
make
sudo make install
```

7. Building clang
```bash
mkdir build && cd build
cmake -G Ninja ../llvm -DLLVM_BUILD_TESTS=ON  # Enable tests; default is off.
ninja
ninja check       # Test LLVM only.
ninja clang-test  # Test Clang only.
ninja install
```
	a. It's a good idea to enable tests the first time, just to make sure everything is set up correctly.
	b. Note from clang.llvm.org:
	`All of the tests should pass, though there is a (very) small chance that you can catch LLVM and Clang out of sync. Running 'git svn rebase' in both the llvm and clang directories should fix any problems.`

8. (optional) Configuring cmake to use clang
I'm personally using the compilers bundled with xcode, but to change the tools cmake/ninja uses, you can run the following. For the MacOS sdks I'm using the sdks bundled with Xcode9 because I'm not on mojave yet, so I had issues using the new sdks in Xcode10. You will want to use the latest version of xcode for the iOS sdks.
```bash
cd clang-llvm/build
ccmake ../llvm
```
Press `t` to enter advanced mode. When you're done press `c`, then `g` once you're back at the original screen.

9. Obtain this tool
```bash
cd clang-llvm/llvm/tools/clang/tools/extra/
git clone IDK_YET objc-unused-imports

echo 'add_subdirectory(objc-unused-imports)' >> CMakeLists.txt
```

10. Building objc-unused-imports
```bash
cd clang-llvm/build
ninja

# The binary will now be located at clang-llvm/build/bin/objc-unused-imports
```
