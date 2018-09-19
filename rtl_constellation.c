/*
 * small program that plots a constellation using rtlsdr
 *
 * glut code copied from http://stackoverflow.com/questions/503816/linux-fastest-way-to-draw
 * rtlsdr code copied from rtl_sdr.c that came with the lib
 *
 * to install the necessary libs on linux: apt-get install libfftw3-3 libfftw3-dev freeglut3-dev libglew-dev
 * on OSX: brew install fftw (glut should be present by default)
 *
 * Compile on linux: gcc -o rtl_waterfall rtl_waterfall.c -lglut -lGL -lrtlsdr -lfftw3 -lm
 * OSX: gcc -framework GLUT -framework OpenGL -framework Cocoa rtl_waterfall.c -o rtl_waterfall -lrtlsdr -lfftw3 -lm
 *
 */

#ifdef __APPLE__
#include <GLUT/glut.h>
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#include <GL/glut.h>
#endif

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "rtl-sdr.h"

#define GLUT_BUFSIZE 512

float texture[GLUT_BUFSIZE][GLUT_BUFSIZE][3];

uint8_t *buffer;

#define DEFAULT_SAMPLE_RATE		2e6
#define DEFAULT_BUF_LENGTH		(8 * 16384)		// 2^17, [min,max]=[512,(256 * 16384)], update freq = (DEFAULT_SAMPLE_RATE / DEFAULT_BUF_LENGTH) Hz

// Not all tuners can go to either extreme...
#define RTL_MIN_FREQ 22e6
#define RTL_MAX_FREQ 1766e6

static int do_exit = 0;
static rtlsdr_dev_t *dev = NULL;
uint32_t frequency;
uint32_t samp_rate = DEFAULT_SAMPLE_RATE;

void glut_renderScene()
{
	glEnable (GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GLUT_BUFSIZE, GLUT_BUFSIZE, GL_RGB, GL_FLOAT, &texture[0][0][0]);

	glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0, -1.0);
		glTexCoord2f(1.0f, 0.0f); glVertex2f(-1.0,  1.0);
		glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0,  1.0);
		glTexCoord2f(0.0f, 1.0f); glVertex2f( 1.0, -1.0);
	glEnd();

	glFlush();
	glutSwapBuffers();
}

void glut_keyboard( unsigned char key, int x, int y )
{
 	switch(key) {
 	case 27: // Escape key
 		fprintf(stderr, "\nbye\n");
 		exit(0);
		break;
	}
}

int glut_init(int *argc,char **argv)
{
	glutInitWindowPosition(100, 100);
	glutInitWindowSize(GLUT_BUFSIZE, GLUT_BUFSIZE);
	glutInit(argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);

	glutCreateWindow("Constellation");

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, GLUT_BUFSIZE, GLUT_BUFSIZE, 0, GL_RGB, GL_FLOAT, &texture[0][0][0]);
	
	glutDisplayFunc(glut_renderScene);

	// add key press check
	// http://stackoverflow.com/questions/10250510/glutmainloop-in-mac-osx-lion-freezes-application
	glutKeyboardFunc(glut_keyboard);
}

void readData(int val) {
	uint32_t out_block_size = DEFAULT_BUF_LENGTH;
	int n_read;
	int r = rtlsdr_read_sync(dev, buffer, out_block_size, &n_read);
	if (r < 0)
	{
		fprintf(stderr, "WARNING: sync read failed.\n");
		return;
	}
	
	if ((uint32_t)n_read < out_block_size)
	{
		fprintf(stderr, "Short read, samples lost, exiting!\n");
		return;
	}
	
	// fade current buffer


	// add new values
	uint32_t N = out_block_size/2;
	int i;
	for(i = 0 ; i < N ; i++)
	{
		double I = (buffer[i*2] -127) * 0.008;		// adc is 8 bits, map (0,255) to (-1,1)
		double Q = (buffer[i*2 +1] -127) * 0.008;
	}

	glutPostRedisplay();
	glutTimerFunc(0,readData,++val);
}


int main(int argc, char **argv)
{
	int c;
	uint32_t dev_index = 0;
	int gain = 0, ppm = 0;
	frequency = 100e6; /* global */

	// setup window
	glut_init(&argc, argv);

	while ((c = getopt(argc, argv, "d:f:g:p:r:")) != -1) {
		switch (c) {
			case 'd':
				dev_index = atoi(optarg);
				break;
			case 'f':
				frequency = atof(optarg); // for scientific notation
				break;
			case 'g':
				gain = (int) (atof(optarg) * 10); // nice clean fractional decibels
				break;
			case 'p':
				ppm = (int) lround(atof(optarg)); // accept fractional ppm correction
				break;
			case 'r':
				samp_rate = atof(optarg); // for scientific notation
				break;
			default:
				fprintf(stderr, "Usage: %s [X11_GLUT_flags] [-d <dev_index>] [-f <freq>] "
					"[-g <gain_dB>] [-p ppm] [-r samp_rate]\n", argv[0]);
				exit(1);
				/* NOTREACHED */
		}
	}
	argv += optind;
	argc -= optind;
	
	///
	// init radio
	///
	int device_count = rtlsdr_get_device_count();
	if (!device_count)
	{
		fprintf(stderr, "No supported devices found.\n");
		exit(1);
	}

	fprintf(stderr, "Found %d device(s):\n", device_count);

	fprintf(stderr, "Using device %d: %s\n", dev_index, rtlsdr_get_device_name(dev_index));

	if (rtlsdr_open(&dev, dev_index) < 0)
	{
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}

	/* Set the sample rate */
	if (rtlsdr_set_sample_rate(dev, samp_rate) < 0)
		fprintf(stderr, "WARNING: Failed to set sample rate.\n");

	/* Set the frequency */
	if ((frequency < RTL_MIN_FREQ) || (frequency > RTL_MAX_FREQ)) {
		frequency = 100e6;
		fprintf(stderr, "WARNING: Center frequency should be %dMHz-%dMHz; setting to %dMHz\n",
			(int)(RTL_MIN_FREQ/1e6), (int)(RTL_MAX_FREQ/1e6), (int)(frequency/1e6));
	}
	if (rtlsdr_set_center_freq(dev, frequency) < 0)
		fprintf(stderr, "WARNING: Failed to set center freq.\n");
	else
		fprintf(stderr, "Tuned to %f MHz.\n", frequency/1e6);

	/* Set the oscillator frequency ("PPM") correction */
	if (ppm)
		if (rtlsdr_set_freq_correction(dev, ppm) < 0)
			fprintf(stderr, "WARNING: Failed to set frequency correction\n");

	if (gain) {
		if (rtlsdr_set_tuner_gain(dev, gain) < 0) {
			fprintf(stderr, "Tuner gain set to %.1f dB.\n", rtlsdr_get_tuner_gain(dev)/10.0);
		}
		else {
			fprintf(stderr, "Invalid gain %.1f; using auto gain\n", gain/10.0);
			if (rtlsdr_set_tuner_gain_mode(dev, 0) < 0) {
				fprintf(stderr, "WARNING: Failed to enable automatic gain.\n");
			}
		}
	}
	
	/* Reset endpoint before we start reading from it (mandatory) */
	if (rtlsdr_reset_buffer(dev) < 0)
		fprintf(stderr, "WARNING: Failed to reset buffers.\n");

	
	/* start reading samples */
	fprintf(stderr, "Update frequency is %.2fHz.\n",((double)DEFAULT_SAMPLE_RATE / (double)DEFAULT_BUF_LENGTH));
	fprintf(stderr, "Press [Q,q,w,W] to change frequency, [a,z] to adjust color sensitivity, [f,g,h] to adjust gain, ESC to quit.\n");

	glutTimerFunc(0,readData,0);
	glutMainLoop();
	
	return 0;
}
