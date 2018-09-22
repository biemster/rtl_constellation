# rtl_constellation
Simple glut based constellation window for rtl-sdr

small program that plots a constellation using rtlsdr  
usage: rtl_constellation [-d &lt;dev_index&gt;] [-f &lt;freq&gt;] [-g &lt;gain_dB&gt;] [-p &lt;ppm&gt;] [-r &lt;samp_rate&gt;]

Press [Q,q,w,W] to change frequency, [a,z] to adjust color sensitivity, [f,g,h] to adjust gain, [e] to toggle spectrum display, ESC to quit.

glut code copied from http://stackoverflow.com/questions/503816/linux-fastest-way-to-draw  
rtlsdr code copied from rtl_sdr.c that came with the lib  

to install the necessary libs on linux: apt-get install libfftw3-3 libfftw3-dev freeglut3-dev libglew-dev  
on OSX: brew install fftw (glut should be present by default)  

Compile on linux: gcc -o rtl_constellation rtl_constellation.c -lglut -lGL -lrtlsdr -lfftw3 -lm  
OSX: gcc -framework GLUT -framework OpenGL -framework Cocoa rtl_constellation.c -o rtl_constellation -lrtlsdr -lfftw3 -lm
(last tested on osx years ago, on 10.6) (If using MacPorts, add -L /opt/local/lib -I /opt/local/include)
