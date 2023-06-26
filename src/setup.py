from distutils.core import setup, Extension
import glob
import os
# the c++ extension module
sources = ['algorithms.c', 'amy.c', 'delay.c', 'envelope.c', 'filters.c', 'libminiaudio-audio.c', 'oscillators.c', 'partials.c', 'pcm.c', 'pyamy.c']
os.environ["CC"] = "gcc"
os.environ["CXX"] = "g++"

extension_mod = Extension("libamy", sources=sources, extra_compile_args=["-I/opt/homebrew/include"], extra_link_args=["-L/opt/homebrew/lib","-lpthread"])

setup(name = "libamy", 
	ext_modules=[extension_mod])