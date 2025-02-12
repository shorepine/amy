from distutils.core import setup, Extension
import glob
import os
# the c++ extension module
sources = ['algorithms.c', 'amy.c', 'delay.c', 'envelope.c', 'filters.c', 'sequencer.c', 'transfer.c', 'custom.c', 'patches.c', 'libminiaudio-audio.c', 'oscillators.c', 'partials.c', 'interp_partials.c', 'pcm.c', 'pyamy.c', 'log2_exp2.c']
os.environ["CC"] = "gcc"
os.environ["CXX"] = "g++"

link_args = ["-L/opt/homebrew/lib","-lpthread"]
if os.uname()[0] == 'Darwin':
	frameworks = ['CoreAudio', 'AudioToolbox', 'AudioUnit', 'CoreFoundation']
	for f in frameworks:
		link_args += ['-framework', f]

if os.uname()[4] == 'armv7l' or os.uname()[4] == 'armv6l':
	link_args += ['-latomic', '-ldl']

extension_mod = Extension("libamy", sources=sources, \
	extra_compile_args=["-I/opt/homebrew/include", "-DAMY_DEBUG"], \
	extra_link_args=link_args)

setup(name = "libamy", 
	ext_modules=[extension_mod])
