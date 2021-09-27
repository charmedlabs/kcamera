from distutils.core import setup, Extension

kencoder = Extension('kencoder', 
	sources = ['kencodermodule.c', 'h264_encoder.cpp','run.cpp' ],
	include_dirs = ['../libcamera/include', '../libcamera/build/include'],
	libraries = [],
	library_dirs =[], 
	extra_compile_args = [ #'-ggdb', '-O1', 
        '-DNDEBUG', '-mfpu=neon-fp-armv8', '-ftree-vectorize', '-O3', 
        '-std=c++17'
    ]
)

setup (name = 'kencoderPackage',
       version = '1.0',
       description = 'kencoder package',
       ext_modules = [kencoder])