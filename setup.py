from distutils.core import setup, Extension
from setuptools import find_packages
import glob
import os
import subprocess
import sys
# the c++ extension module
sources = ['algorithms.c', 'amy.c', 'delay.c', 'envelope.c', 'filters.c', 'parse.c', 'sequencer.c', 'transfer.c', 'midi_mappings.c', 'custom.c', 'patches.c', 'libminiaudio-audio.c', 'oscillators.c', 'interp_partials.c', 'pcm.c', 'pyamy.c', 'log2_exp2.c', 'instrument.c', 'amy_midi.c', 'api.c', 'cv_trigger.c']

for i in range(len(sources)):
	sources[i] = "src/"+sources[i]

os.environ["CC"] = "gcc"
os.environ["CXX"] = "g++"

comp_args = ["-I/opt/homebrew/include", "-DAMY_DEBUG", "-Wno-unused-but-set-variable", "-Wno-unreachable-code", "-DAMY_WAVETABLE"]
link_args = ["-L/opt/homebrew/lib","-lpthread"]

# Bake the Gamma9001 drum banks (kits at patches 384-390) into the module, like
# the web build: generate build/drums_bin.c from sounds/gamma9001/ and link it.
gamma_manifest = os.path.join('sounds', 'gamma9001', 'manifest.json')
gamma_drums_bin_c = os.path.join('build', 'drums_bin.c')
if os.path.exists(gamma_manifest):
	if not os.path.exists(gamma_drums_bin_c) or \
			os.path.getmtime(gamma_drums_bin_c) < os.path.getmtime(gamma_manifest):
		subprocess.check_call([sys.executable, '-m', 'amy.headers', 'gamma9001'])
	sources.append(gamma_drums_bin_c)
	comp_args.append("-DGAMMA9001")

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
