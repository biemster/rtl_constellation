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
#include <stdbool.h>

#include <fftw3.h>
#include <complex.h>

#include "rtl-sdr.h"

#define GLUT_BUFSIZE 512
#define SPECTRUM_LINES 128
bool show_spectrum;

float texture[GLUT_BUFSIZE][GLUT_BUFSIZE][3];
#define FBUF_LEN 8 // room for {'1', '7', '6', '6', '.', '0', '\0'}
char strFreq[FBUF_LEN];
float pwr_max;
float pwr_diff;

uint8_t *buffer;
uint8_t *pll;
fftw_complex *fftw_in;
fftw_complex *fftw_out;
fftw_plan fftw_p;

#define DEFAULT_BUF_LENGTH		2048 * 1		//  [min,max]=[512,(256 * 16384)], update freq = (DEFAULT_SAMPLE_RATE / DEFAULT_BUF_LENGTH) Hz
#define DEFAULT_SAMPLE_RATE		2048 * 1e3		// The sample rates are dictated by the RTL2832U chip, not the tuner chip.
												// The RTL2832U can sample from two ranges ...
												// 225001 to 300000 and 900001 to 3200000.
												// Pick any number that lies in either of those two ranges.

#define PLL_LOCK_STEPS 10

// Not all tuners can go to either extreme...
#define RTL_MIN_FREQ 22e6
#define RTL_MAX_FREQ 1766e6

static int do_exit = 0;
static rtlsdr_dev_t *dev = NULL;
uint32_t frequency;
uint32_t samp_rate = DEFAULT_SAMPLE_RATE;

int *gains = NULL, gainsteps = 0;
int gain_get_levels();
int gain_auto_enable();
int gain_manual_enable();
int gain_is_valid(int);
int gain_manual_increase();
int gain_manual_decrease();

void displayTicks();
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

	displayTicks();

	glFlush();
	glutSwapBuffers();
}

void glut_keyboard( unsigned char key, int x, int y )
{
	uint32_t new_freq;
	int r;
 	switch(key)
 	{
	case 'Q':
		new_freq = frequency - samp_rate/4;
		if (new_freq < RTL_MIN_FREQ)
			new_freq = RTL_MIN_FREQ;
		if (rtlsdr_set_center_freq(dev, new_freq) < 0)
			fprintf(stderr, "WARNING: Failed to set center freq.\n");
		else {
			fprintf(stderr, "\rTuned to %f MHz.", new_freq/1e6);
			frequency = new_freq;
		}
		snprintf(strFreq, FBUF_LEN, "%6.1f",frequency/1e6);
		break;
	case 'q':
		new_freq = frequency - samp_rate/2;
		if (new_freq < RTL_MIN_FREQ)
			new_freq = RTL_MIN_FREQ;
		if (rtlsdr_set_center_freq(dev, new_freq) < 0)
			fprintf(stderr, "WARNING: Failed to set center freq.\n");
		else {
			fprintf(stderr, "\rTuned to %f MHz.", new_freq/1e6);
			frequency = new_freq;
		}
		snprintf(strFreq, FBUF_LEN, "%6.1f",frequency/1e6);
		break;
	case 'W':
		new_freq = frequency + samp_rate/4;
		if (new_freq > RTL_MAX_FREQ)
			new_freq = RTL_MAX_FREQ;
		if (rtlsdr_set_center_freq(dev, new_freq) < 0)
			fprintf(stderr, "WARNING: Failed to set center freq.\n");
		else {
			fprintf(stderr, "\rTuned to %f MHz.", new_freq/1e6);
			frequency = new_freq;
		}
		snprintf(strFreq, FBUF_LEN, "%6.1f",frequency/1e6);
		break;
	case 'w':
		new_freq = frequency + samp_rate/2;
		if (new_freq > RTL_MAX_FREQ)
			new_freq = RTL_MAX_FREQ;
		if (rtlsdr_set_center_freq(dev, new_freq) < 0)
			fprintf(stderr, "WARNING: Failed to set center freq.\n");
		else {
			fprintf(stderr, "\rTuned to %f MHz.", new_freq/1e6);
			frequency = new_freq;
		}
		snprintf(strFreq, FBUF_LEN, "%6.1f",frequency/1e6);
 		break;
 	case 'a':
 		pwr_diff *= .5;
 		fprintf(stderr, "\rpwr_diff reset to %.4f", pwr_diff);
 		break;
 	case 'z':
 		pwr_diff *= 2.;
 		fprintf(stderr, "\rpwr_diff reset to %.4f", pwr_diff);
 		break;
	case 'f':
		gain_manual_decrease();
		break;
	case 'g':
		gain_auto_enable();
		break;
	case 'h':
		gain_manual_increase();
		break;
	case 'e':
		if(show_spectrum) {
			show_spectrum = false;
		}
		else {
			show_spectrum = true;
		}
		break;
 	case 27: // Escape key
 		fprintf(stderr, "\nbye\n");
 		exit(0);
		break;
	}
}

void displayTicks()
{
	char tbuf[FBUF_LEN];
	snprintf(tbuf, FBUF_LEN, "%0.2f", samp_rate/4e6);
	glDisable(GL_TEXTURE_2D);
		glRasterPos2f(.95,-.98);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, '|');

		glRasterPos2f(.712,-1);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, '|');

		glRasterPos2f(.475,-.98);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, '|');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, ' ');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, '+');
		for (int i = 0; i < strlen(tbuf); i++)
			glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, tbuf[i]);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, 'M');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, 'H');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, 'z');

		glRasterPos2f(.237,-1);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, '|');

		glRasterPos2f(-.005,-.98);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, '|');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, ' ');
		for (int i = 0; i < strlen(strFreq); i++)
			glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, strFreq[i]);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, ' ');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, 'M');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, 'H');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, 'z');

		glRasterPos2f(-.245,-1);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, '|');

		glRasterPos2f(-.482,-.98);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, '|');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, ' ');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, '-');
		for (int i = 0; i < strlen(tbuf); i++)
			glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, tbuf[i]);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, 'M');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, 'H');
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, 'z');

		glRasterPos2f(-.72,-1);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, '|');

		glRasterPos2f(-.962,-.98);
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, '|');
	glEnable(GL_TEXTURE_2D);
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

float pll_lock_ls() {
	// IMPROVE: add to loop in readData()
	// http://liquidsdr.org/blog/pll-simple-howto/
	// least squares: https://stackoverflow.com/questions/5083465/fast-efficient-least-squares-fit-algorithm-in-c

	// parameters and simulation options
	float alpha             =  0.5f;   // phase adjustment factor

	// initialize states
	float beta              = 0.5*alpha*alpha; // frequency adjustment factor
	float phase_out         = 0.0f;            // output signal phase
	float frequency_out     = 0.0f;            // output signal frequency

	int pll_searching = PLL_LOCK_STEPS;
	float sumx  = 0.0f;
	float sumx2 = 0.0f;
	float sumxy = 0.0f;
	float sumy  = 0.0f;
	float sumy2 = 0.0f;

	uint32_t N = DEFAULT_BUF_LENGTH/2;
	int nls = 0;
	int i;
	for(i = 0 ; i < N ; i++)
	{
		float sigI = (buffer[i*2] -127) * 0.008;		// adc is 8 bits, map (0,255) to (-1,1)
		float sigQ = (buffer[i*2 +1] -127) * 0.008;
		float complex signal_in  = sigI+sigQ*I;
		float complex signal_out = cexpf(_Complex_I * phase_out);

		pll[i*2] = (crealf(signal_out) / 0.008) + 127;
		pll[i*2 +1] = (cimagf(signal_out) / 0.008) + 127;

		// compute phase error estimate
		float phase_error = cargf( signal_in * conjf(signal_out) );

		// apply loop filter and correct output phase and frequency
		phase_out     += alpha * phase_error;    // adjust phase
		frequency_out +=  beta * phase_error;    // adjust frequency

		// increment output phase values
		phase_out += frequency_out;

		// if the phase error is small, the loop is locked and we can use the frequency in the average
		if(fabs(phase_error) < 1) {
			if(!pll_searching) {
				sumx  += i;
				sumx2 += i*i;
				sumxy += i * phase_out;
				sumy  += phase_out;
				// sumy2 += phase_out * phase_out;
				nls++;
			}
			else {
				pll_searching--;
			}
		}
		else {
			pll_searching = PLL_LOCK_STEPS;
		}
	}

	// calculate linear regression y = mx + b
	float denom = (nls * sumx2) - (sumx*sumx);
	//if(denom == 0) {
	//	// not going to happen
	//	return 0.;
	//}
	float m = ((nls * sumxy) - (sumx * sumy)) / denom;
	// b = ((sumy * sumx2) - (sumx * sumxy)) / denom;

	return m;
}

float pll_lock() {
	// IMPROVE: add to loop in readData()
	// http://liquidsdr.org/blog/pll-howto/

	// parameters
	float wn                = 0.1f;    // pll bandwidth
	float zeta              = 0.707f;   // pll damping factor
	float K                 = 1000;     // pll loop gain

	// generate loop filter parameters (active PI design)
	float t1 = K/(wn*wn);   // tau_1
	float t2 = 2*zeta/wn;   // tau_2

	// feed-forward coefficients (numerator)
	float b0 = (4*K/t1)*(1.+t2/2.0f);
	float b1 = (8*K/t1);
	float b2 = (4*K/t1)*(1.-t2/2.0f);

	// feed-back coefficients (denominator)
	//    a0 =  1.0  is implied
	float a1 = -2.0f;
	float a2 =  1.0f;

	// filter buffer
	float v0=0.0f, v1=0.0f, v2=0.0f;
	
	// initialize states
	float phi_hat = 0.0f;           // PLL's initial phase
	float phi_hat_avg = 0.0f;

	int pll_searching = PLL_LOCK_STEPS;

	uint32_t N = DEFAULT_BUF_LENGTH/2;
	int navg = 0;
	int i;
	float complex x;
	float complex y;
	for(i = 0 ; i < N ; i++)
	{
		float sigI = (buffer[i*2] -127) * 0.008;		// adc is 8 bits, map (0,255) to (-1,1)
		float sigQ = (buffer[i*2 +1] -127) * 0.008;
		x = sigI + sigQ*I;

		// compute PLL output from phase estimate
		y = cosf(phi_hat) + _Complex_I*sinf(phi_hat);
		float phi_hat_prev = phi_hat;

		// compute error estimate
		float delta_phi = cargf( x * conjf(y) );

		// push result through loop filter, updating phase estimate

		// advance buffer
		v2 = v1;  // shift center register to upper register
		v1 = v0;  // shift lower register to center register

		// compute new lower register
		v0 = delta_phi - v1*a1 - v2*a2;

		// compute new output
		phi_hat = v0*b0 + v1*b1 + v2*b2;

		// if the phase error is small, the loop is locked and we can use the frequency in the average
		if(fabs(delta_phi) < 1) {
			if(!pll_searching) {
				phi_hat_avg += phi_hat - phi_hat_prev;
				navg++;
			}
			else {
				pll_searching--;
			}
		}
		else {
			pll_searching = PLL_LOCK_STEPS;
		}
	}

	return navg ? phi_hat_avg / navg : 0.;
}

void readData(int line_idx) {	
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

	float pll_freq = pll_lock_ls();
	fprintf(stderr, "\rPLL locked on %.07f", pll_freq);
	
	// fade current buffer
	int i,j;
	for(i = 0 ; i < GLUT_BUFSIZE ; i++) {
		for(j = show_spectrum ? SPECTRUM_LINES : 0 ; j < GLUT_BUFSIZE ; j++) {
			if(texture[i][j][0] > 0.9f) {
				texture[i][j][0] = 0.5f;
				texture[i][j][1] = 0.5f;
				texture[i][j][2] = 0.0f;
			}
			else if(texture[i][j][0] > 0.4f) {
				texture[i][j][0] = 0.0f;
				texture[i][j][1] = 1.0f;
				texture[i][j][2] = 0.0f;
			}
			else if(texture[i][j][1] > 0.9f) {
				texture[i][j][0] = 0.0f;
				texture[i][j][1] = 0.5f;
				texture[i][j][2] = 0.5f;
			}
			else if(texture[i][j][1] > 0.4f) {
				texture[i][j][0] = 0.0f;
				texture[i][j][1] = 0.0f;
				texture[i][j][2] = 1.0f;
			}
			else if(texture[i][j][2] > 0.9f) {
				texture[i][j][0] = 0.0f;
				texture[i][j][1] = 0.0f;
				texture[i][j][2] = 0.5f;
			}
			else if(texture[i][j][2] > 0.4f) {
				texture[i][j][0] = 0.0f;
				texture[i][j][1] = 0.0f;
				texture[i][j][2] = 0.0f;
			}
		}
	}

	// add new values
	uint32_t N = out_block_size/2;	
	float i_flt = 0.;
	for(i = PLL_LOCK_STEPS ; i < N ; i++) {
		// phase align constellation: find starting index
		if(cargf(pll[i*2] + pll[i*2 +1]*I) < pll_freq) {
			i_flt = (float)i;
			// fprintf(stderr, "\rstart idx %04d", i);
			break;
		}
	}
	for(i = 0 ; i < N ; i++) {
		if(i == (int)(i_flt + ((2*M_PI) / pll_freq)) && pll_freq > 0.) {
			// draw constellation points
			int sigI = (int)((buffer[i*2] / 255.) * GLUT_BUFSIZE);		// adc is 8 bits, map (0,255) to (0,1) * GLUT_BUFSIZE
			int sigQ = (int)((buffer[i*2 +1] / 255.) * GLUT_BUFSIZE);

			if(sigI >= 0 && sigQ >= 0 &&
				sigI < GLUT_BUFSIZE && sigQ < GLUT_BUFSIZE &&
				!(show_spectrum && sigQ < SPECTRUM_LINES)) {
				texture[sigI][sigQ][0] = 1.0f; // red
				texture[sigI][sigQ][1] = 0.0f; // green
				texture[sigI][sigQ][2] = 0.0f; // blue
			}
			
			i_flt += (2*M_PI) / pll_freq;
		}

		if(show_spectrum) {
			fftw_in[i][0] = (buffer[i*2] -127) * 0.008;		// adc is 8 bits, map (0,255) to (-1,1)
			fftw_in[i][1] = (buffer[i*2 +1] -127) * 0.008;
		}

		// fprintf(stdout, "%.4f,%.4f\n", (buffer[i*2] -127) * 0.008,(buffer[i*2 +1] -127) * 0.008);
	}
	// fprintf(stdout, "---- %d:%.8f\n", line_idx, pll_freq);

	if(show_spectrum) {
		fftw_execute(fftw_p);
		//fprintf(stderr, "%d\n", n_read);
		//fprintf(stderr, "%3d: %d %d %d %d %d %d %d %d %d %d\n", line_idx, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7], buffer[8], buffer[9] );
		//fprintf(stderr, "%3d: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", line_idx, fftw_in[0][0], fftw_in[0][1], fftw_in[1][0], fftw_in[1][1], fftw_in[2][0], fftw_in[2][1], fftw_in[3][0], fftw_in[3][1], fftw_in[4][0], fftw_in[4][1] );
		//fprintf(stderr, "%3d: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", line_idx, fftw_out[0][0], fftw_out[0][1], fftw_out[1][0], fftw_out[1][1], fftw_out[2][0], fftw_out[2][1], fftw_out[3][0], fftw_out[3][1], fftw_out[4][0], fftw_out[4][1] );
	
			
		// put fft on screen
		// x = index of pixel on screen, n_avg is number of fft bins that have to be averaged in one pixel
		int n_avg = N / GLUT_BUFSIZE;
		float pwr, color_idx, color_blue, color_green, color_red;
		int x, p;
		for(x = 0 ; x < GLUT_BUFSIZE ; x++)
		{
			pwr = 0.0f;
			for(p = 0 ; p < n_avg ; p++)
				pwr += (fftw_out[(x*n_avg) +p][0] * fftw_out[(x*n_avg) +p][0]) + (fftw_out[(x*n_avg) +p][1] * fftw_out[(x*n_avg) +p][1]);
			pwr /= (n_avg * (N/2));
			
			// scale colors to power in spectrum
			if(pwr > pwr_max)
				pwr_max = pwr;
			color_idx = pwr/pwr_diff;
			
			//color_idx = (float)x/(float)GLUT_BUFSIZE;
			if(color_idx < 1)
			{
				//color_blue = (sin((color_idx * 3.1415f) + 1.5708) +1)/2;
				//color_green = sin(color_idx * 3.1415f);
				//color_red = (sin((color_idx * 3.1415f) - 1.5708) +1)/2;
				color_blue = exp( -((color_idx - 0.1) / .3) * ((color_idx - 0.1) / .3) );
				color_green = exp( -((color_idx - 0.35) / .5) * ((color_idx - 0.35) / .5) );
				color_red = exp( -((color_idx - .95) / .3) * ((color_idx - .95) / .3) );
			}
			else
			{
				color_blue = 0.0f;
				color_green = 0.0f;
				color_red = 1.0f;
			}
			
			// negative frequencies are in [N/2,N] and positive in [0,N/2]
			int xN;
			if(x < (GLUT_BUFSIZE/2))
				xN = x + GLUT_BUFSIZE/2;
			else
				xN = x - GLUT_BUFSIZE/2;
	
			for(int y = SPECTRUM_LINES; y > 0; y--) {
				texture[xN][y][0] = texture[xN][y-1][0];
				texture[xN][y][1] = texture[xN][y-1][1];
				texture[xN][y][2] = texture[xN][y-1][2];
			}
			texture[xN][0][0] = color_red;
			texture[xN][0][1] = color_green;
			texture[xN][0][2] = color_blue;
		}
		//fprintf(stderr, "%.2f\n", pwr_max);
	}

	glutPostRedisplay();
	glutTimerFunc(0,readData,++line_idx);
}

int gain_get_levels() {
	gainsteps = rtlsdr_get_tuner_gains(dev, NULL);
	gains = calloc(gainsteps, sizeof(int));
	gainsteps = rtlsdr_get_tuner_gains(dev, gains);
	return gainsteps;
}

int gain_auto_enable() {
	int rv;
	if ((rv = rtlsdr_set_tuner_gain_mode(dev, 0)) < 0)
		fprintf(stderr, "WARNING: Failed to enable automatic gain.\n");
	return rv;
}

int gain_manual_enable() {
	int rv;
	if ((rv = rtlsdr_set_tuner_gain_mode(dev, 1)) < 0)
		fprintf(stderr, "WARNING: Failed to enable manual gain.\n");
	return rv;
}

int gain_is_valid(int g) {
	int i;
	if ((NULL == gains) || (0 == gainsteps))
		return 0;

	for (i = 0; i < gainsteps; i++)
		if (gains[i] == g)
			return 1;
	return 0;
}

int gain_manual_increase() {
	int g = rtlsdr_get_tuner_gain(dev);
	gain_manual_enable();
	for (int i = 0; i < gainsteps; i++)
		if (gains[i] > g) {
			rtlsdr_set_tuner_gain(dev, gains[i]);
			return 0;
		}
	return -1;
}

int gain_manual_decrease() {
	int g = rtlsdr_get_tuner_gain(dev);
	gain_manual_enable();
	for (int i = gainsteps-1; i ; i--)
		if (gains[i] < g) {
			rtlsdr_set_tuner_gain(dev, gains[i]);
			return 0;
		}
	return -1;
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
	snprintf(strFreq, FBUF_LEN, "%6.1f",frequency/1e6);

	/* Set the oscillator frequency ("PPM") correction */
	if (ppm)
		if (rtlsdr_set_freq_correction(dev, ppm) < 0)
			fprintf(stderr, "WARNING: Failed to set frequency correction\n");

	/* Set the gain */
	gain_manual_enable(); // set manual mode first so gain levels can be queried
	gain_get_levels();
	gain_auto_enable(); // switch back to auto gain mode

	if (gain) {
		if (gain_is_valid(gain)) {
			if (rtlsdr_set_tuner_gain(dev, gain) < 0)
				fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
			fprintf(stderr, "Tuner gain set to %.1f dB.\n", rtlsdr_get_tuner_gain(dev)/10.0);
		} else {
			fprintf(stderr, "Invalid gain %.1f; using auto gain\n", gain/10.0);
			fprintf(stderr, "Allowed values: ");
			for (int i = 0; i < gainsteps; i++)
				fprintf(stderr, "%.1f ", gains[i]/10.0);
			fprintf(stderr, "\n\n");
		}
	}
	
	/* Reset endpoint before we start reading from it (mandatory) */
	if (rtlsdr_reset_buffer(dev) < 0)
		fprintf(stderr, "WARNING: Failed to reset buffers.\n");

	
	///
	// setup fftw
	///
	uint32_t out_block_size = DEFAULT_BUF_LENGTH;
	buffer = malloc(out_block_size * sizeof(uint8_t));
	pll = malloc(out_block_size * sizeof(uint8_t));
	fftw_in = fftw_malloc ( sizeof ( fftw_complex ) * out_block_size/2 );
	fftw_out = fftw_malloc ( sizeof ( fftw_complex ) * out_block_size/2 );
	
	// put the plan on FFTW_MEASURE to calculate the optimal fft plan (takes a few seconds).
	// If performance of FFTW_ESTIMATE is good enough use that one
	//fftw_p = fftw_plan_dft_1d ( out_block_size/2, fftw_in, fftw_out, FFTW_FORWARD, FFTW_MEASURE );
	fftw_p = fftw_plan_dft_1d ( out_block_size/2, fftw_in, fftw_out, FFTW_FORWARD, FFTW_ESTIMATE );

	
	/* start reading samples */
	fprintf(stderr, "Update frequency is %.2fHz.\n",((double)DEFAULT_SAMPLE_RATE / ((double)DEFAULT_BUF_LENGTH)));
	fprintf(stderr, "Press [Q,q,w,W] to change frequency, [a,z] to adjust color sensitivity, [f,g,h] to adjust gain, [e] to toggle spectrum, ESC to quit.\n");
	pwr_max = 0.0f;
	pwr_diff = 1.0f;
	glutTimerFunc(0,readData,0);
	glutMainLoop();
	
	return 0;
}
