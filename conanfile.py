import os
from sys import platform
from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
from conan.tools.scm import Git
from conan.tools.files import copy
from pathlib import Path

class VtxGromacsRecipe(ConanFile):
    name = "gromacs"
    version = "2026.1"
    package_type = "library"
    revision_mode = "hash"

    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False]
        , "fPIC": [True, False]
    }
    default_options = {
        "shared": False
        , "fPIC": True
    }

    generators = "CMakeDeps", "CMakeToolchain"

    exports_sources = "CMakeLists.txt", "src/*", "cmake/*", "share/*", "tests/*", "api/*", "COPYING", "AUTHORS", "CITATION.cff", "docs/*", "scripts/*", "admin/*"

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self)

    def build_requirements(self):
        self.tool_requires("cmake/3.27.0")

    def build(self):
        cmake = CMake(self)
        cmake_args = [
            "-DGMX_FFT_LIBRARY=fftpack"
            , "-DBUILD_SHARED_LIBS=off"
            , "-DGMX_PREFER_STATIC_LIBS=on"
            , "-DGMX_BUILD_SHARED_EXE=OFF"
        ]
        if platform =='Darwin':
            cmake_args.append("-DGMX_OPENMP=OFF")
        cmake.configure(cli_args=cmake_args) # build with slow fft algorithm. Since we won't use mdrun, it doesn't really matter
        cmake.build()

    def package(self):
        copy(self, pattern="*.dll", src=os.path.join(self.build_folder, "bin"), dst=os.path.join(self.package_folder, "external", "tools", "mdprep", "gromacs"))
        copy(self, pattern="*.exe", src=os.path.join(self.build_folder, "bin"), dst=os.path.join(self.package_folder, "external", "tools", "mdprep", "gromacs"))
        copy(self, pattern="*.so", src=os.path.join(self.build_folder, "bin"), dst=os.path.join(self.package_folder, "external", "tools", "mdprep", "gromacs"))
        copy(self, pattern="gmx", src=os.path.join(self.build_folder, "bin"), dst=os.path.join(self.package_folder, "external", "tools", "mdprep", "gromacs"))
        copy(self, pattern="*", src=os.path.join(self.source_folder, "share","top"), dst=os.path.join(self.package_folder, "data", "tools", "mdprep", "gromacs", "top"))

    def package_info(self):
        self.cpp_info.includedirs = ['include', os.path.join('api', 'legacy', 'include') ]




