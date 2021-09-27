from distutils.core import setup, Extension

kcamera = Extension('kcamera', 
	sources = ['kcameramodule.c', 'kcamera.c', 'dobj.c', 'streamer.c', 'framelist.c', 
        'run.cpp'],
	include_dirs = ['./libcamera/include', './libcamera/build/include'],
       library_dirs =['./libcamera/build/src/libcamera'], 
	libraries = ['camera'],
	extra_compile_args = [ #'-ggdb', '-O1', 
        '-DNDEBUG', '-mfpu=neon-fp-armv8', '-ftree-vectorize', '-O3', 
        '-std=c++17'

       ],
       extra_link_args = ['-Wl,-rpath,$ORIGIN']
)

setup (name = 'kcameraPackage',
       version = '1.0',
       description = 'kcamera package',
       ext_modules = [kcamera])