from distutils.core import setup, Extension
from setuptools import find_packages
import glob
import os
# the c++ extension module
sources = ['algorithms.c', 'amy.c', 'delay.c', 'envelope.c', 'filters.c', 'parse.c', 'sequencer.c', 'transfer.c', 'midi_mappings.c', 'custom.c', 'patches.c', 'libminiaudio-audio.c', 'oscillators.c', 'interp_partials.c', 'pcm.c', 'pyamy.c', 'log2_exp2.c', 'instrument.c', 'amy_midi.c', 'api.c']

for i in range(len(sources)):
	sources[i] = "src/"+sources[i]

os.environ["CC"] = "gcc"
os.environ["CXX"] = "g++"

comp_args = ["-I/opt/homebrew/include", "-DAMY_DEBUG", "-Wno-unused-but-set-variable", "-Wno-unreachable-code"]
link_args = ["-L/opt/homebrew/lib","-lpthread"]

if os.uname()[0] == 'Darwin':
	frameworks = ['CoreAudio', 'AudioToolbox', 'AudioUnit', 'CoreFoundation', 'CoreMIDI', 'Cocoa']
	sources += ['src/macos_midi.m']
	for f in frameworks:
		link_args += ['-framework', f, '-lstdc++']
	comp_args += ['-DMACOS']

if os.uname()[4] == 'armv7l' or os.uname()[4] == 'armv6l':
	link_args += ['-latomic', '-ldl']

extension_mod = Extension("c_amy", sources=sources, \
	extra_compile_args=comp_args, \
	extra_link_args=link_args)

setup(name = "amy", 
	packages=find_packages(include=['amy']),
	ext_modules=[extension_mod])
