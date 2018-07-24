#!/usr/bin/env python

from distutils.core import setup, Extension
from distutils.sysconfig import customize_compiler
from distutils.command.build_ext import build_ext

import os
import glob


# https://stackoverflow.com/questions/8106258/cc1plus-warning-command-line-option-wstrict-prototypes-is-valid-for-ada-c-o/9740721
# Avoid a gcc warning below:
# cc1plus: warning: command line option ‘-Wstrict-prototypes’ is valid
# for C/ObjC but not for C++
class BuildExt(build_ext):
    def build_extensions(self):
        customize_compiler(self.compiler)
        try:
            self.compiler.compiler_so.remove("-Wstrict-prototypes")
        except (AttributeError, ValueError):
            pass
        build_ext.build_extensions(self)


# cpp sources
sources = glob.glob(os.path.join('src', 'audacity', '*.cpp'))
sources += ['src/pyaudacity/noiseredmodule.cpp']

# additional CFLAGS
extra_compile_args = ['-std=c++14', '-Wextra', '-pedantic',
                      '-Wno-unused-parameter', '-Wno-unused-variable',
                      '-Wno-implicit-fallthrough']

# create build module
module = Extension(name='cmodule',
                   # define_macros=[('MAJOR_VERSION', '2'), ('MINOR_VERSION', '1')],
                   libraries=['stdc++', 'sndfile', 'soxr'],
                   language='c++14',
                   extra_compile_args=extra_compile_args,
                   include_dirs=['src/audacity'],
                   sources=sources,
                   )

# run build
setup(name='pyaudacity',
      cmdclass={'build_ext': BuildExt},
      version='0.1',
      ext_package='pyaudacity',
      packages=['pyaudacity'],
      package_dir={'pyaudacity': 'src/pyaudacity'},
      description='Audacity noise reduction python porting library.',
      author='photom',
      license='GPL-2.0',
      author_email='photometrician@gmail.com',
      url='https://github.com/photom/pyaudacity_noisered',
      ext_modules=[module])
