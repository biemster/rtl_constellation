# rtl_waterfall
Simple GL based waterfall window for rtl-sdr

small program that plots a waterfall using rtlsdr
usage: rtl_waterfall freq(MHz) gain

glut code copied from http://stackoverflow.com/questions/503816/linux-fastest-way-to-draw
rtlsdr code copied from rtl_sdr.c that came with the lib

to install the necessary libs on linux: apt-get install libfftw3-3 libfftw3-dev freeglut3-dev libglew-dev
on OSX: brew install fftw (glut should be present by default)

Compile on linux: gcc -o rtl_waterfall rtl_waterfall.c -lglut -lGL -lrtlsdr -lfftw3 -lm
OSX: gcc -framework GLUT -framework OpenGL -framework Cocoa rtl_waterfall.c -o rtl_waterfall -lrtlsdr -lfftw3 -lm
(last tested on osx years ago, on 10.6)