from distutils.core import setup, Extension
import glob
import os
# the c++ extension module
sources = ['algorithms.c', 'amy.c', 'delay.c', 'envelope.c', 'filters.c', 'libminiaudio-audio.c', 'oscillators.c', 'partials.c', 'pcm.c', 'pyamy.c']
os.environ["CC"] = "gcc"
os.environ["CXX"] = "g++"

link_args = ["-L/opt/homebrew/lib","-lpthread"]
if os.uname()[0] == 'Darwin':
	frameworks = ['CoreAudio', 'AudioToolbox', 'AudioUnit', 'CoreFoundation']
	for f in frameworks:
		link_args += ['-framework', f]

extension_mod = Extension("libamy", sources=sources, \
	extra_compile_args=["-I/opt/homebrew/include"], \
	extra_link_args=link_args)

setup(name = "libamy", 
	ext_modules=[extension_mod])
