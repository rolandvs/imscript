// CPU : the definitive interface for image processing practitioners

// cc -O2 cpu.c iio.o -o cpu -lX11 -ltiff -ljpeg -lpng -lz -lm
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "fancy_image.h"

#ifndef FTR_BACKEND
#define FTR_BACKEND 'x'
#endif
#include "ftr.h"

#define OMIT_MAIN_FONTU
#include "fontu.c" // todo: cherry-pick the required fontu functions
#include "fonts/xfonts_all.c"

#include "xmalloc.c"

#define WHEEL_FACTOR 2
#define MAX_PYRAMID_LEVELS 30

// data structure for the image viewer
// this data goes into the "userdata" field of the FTR window structure
struct pan_state {
	// 1. image data
	int w, h;
	//float *frgb;
	struct fancy_image *i;

	// 2. view port parameters
	double zoom_factor, offset_x, offset_y;
	double a, b;
	double aaa[3], bbb[3];

	// 4. roi
	int roi; // 0=nothing 1=dftwindow 2=rawfourier 3=ppsmooth
	int roi_x, roi_y, roi_w;

	// 5. user interface
	struct bitmap_font font[1];
};

// change of coordinates: from window "int" pixels to image "double" point
static void window_to_image(double p[2], struct pan_state *e, int i, int j)
{
	p[0] = e->offset_x + i / e->zoom_factor;
	p[1] = e->offset_y + j / e->zoom_factor;
}

// change of coordinates: from image "double" point to window "int" pixel
static void image_to_window(int i[2], struct pan_state *e, double x, double y)
{
	i[0] = floor((x - e->offset_x) * e->zoom_factor );
	i[1] = floor((y - e->offset_y) * e->zoom_factor );
}

static void get_rgb_from_vec(float *rgb, struct pan_state *e, float *vec)
{
	rgb[0] = rgb[1] = rgb[2] = vec[0];
	if (e->i->pd > 1)
		rgb[1] = rgb[2] = vec[1];
	if (e->i->pd > 2)
		rgb[2] = vec[2];
}

// evaluate the value a position (p,q) in image coordinates
static void pixel(float *out, struct pan_state *e, double p, double q)
{
	if(p<0||q<0){out[0]=out[1]=out[2]=127;return;}// TODO: kill this
	if(p>=e->i->w||q>=e->i->h){out[0]=out[1]=out[2]=0;return;}

	float vec[e->i->pd];
	int oct = 0;
	if (e->zoom_factor < 0.9999)
	{
		int s = round(log2(1/e->zoom_factor));
		if (s < 0) s = 0;
		if (s >= MAX_PYRAMID_LEVELS) s = MAX_PYRAMID_LEVELS-1;
		int sfac = 1<<(s);
		oct = s;
		p /= sfac;
		q /= sfac;
	}
	for (int i = 0; i < e->i->pd; i++)
		vec[i] = fancy_image_getsample_oct(e->i, oct, p, q, i);
	get_rgb_from_vec(out, e, vec);
}

static void action_print_value_under_cursor(struct FTR *f, int x, int y)
{
	if (x<f->w && x>=0 && y<f->h && y>=0) {
		struct pan_state *e = f->userdata;
		double p[2];
		window_to_image(p, e, x, y);
		float c[3];
		pixel(c, e, p[0], p[1]);
		//interpolate_at(c, e->frgb, e->w, e->h, p[0], p[1]);
		//printf("%g\t%g\t: %g\t%g\t%g\n", p[0], p[1], c[0], c[1], c[2]);
		printf("not implemented %g %g : %g %g %g\n",
				p[0], p[1], c[0], c[1], c[2]);
	}
}

static void action_offset_viewport(struct FTR *f, int dx, int dy)
{
	struct pan_state *e = f->userdata;
	e->offset_x -= dx/e->zoom_factor;
	e->offset_y -= dy/e->zoom_factor;

	f->changed = 1;
}

static void action_reset_zoom_and_position(struct FTR *f)
{
	struct pan_state *e = f->userdata;

	e->zoom_factor = 1;
	e->offset_x = 0;
	e->offset_y = 0;
	e->a = 1;
	e->b = 0;
	e->bbb[0] = e->bbb[1] = e->bbb[2] = 0;

	e->roi = 0;
	e->roi_x = f->w / 2;
	e->roi_y = f->h / 2;
	e->roi_w = 73; // must be odd

	f->changed = 1;
}

static void action_contrast_change(struct FTR *f, float afac, float bshift)
{
	struct pan_state *e = f->userdata;

	e->a *= afac;
	e->b += bshift;

	f->changed = 1;
}

static void action_qauto(struct FTR *f)
{
	struct pan_state *e = f->userdata;

	float m = INFINITY, M = -m;
	int pid = 3;
	m = 0;
	M = 255;
	//for (int i = 0; i < 3 * e->pyr_w[pid] * e->pyr_h[pid]; i++)
	//{
	//	float g = e->pyr_rgb[pid][i];
	//	m = fmin(m, g);
	//	M = fmax(M, g);
	//}

	e->a = 255 / ( M - m );
	e->b = 255 * m / ( m - M );
	e->bbb[0] = e->b;
	e->bbb[1] = e->b;
	e->bbb[2] = e->b;

	f->changed = 1;
}

static void action_toggle_roi(struct FTR *f, int x, int y, int dir)
{
	struct pan_state *e = f->userdata;
	e->roi = (e->roi + (dir?-1:1)) % 4;
	fprintf(stderr, "ROI SWITCH(%d) = %d\n", dir, e->roi);
	e->roi_x = x - e->roi_w / 2;
	e->roi_y = y - e->roi_w / 2;
	f->changed = 1;
}

static void action_roi_embiggen(struct FTR *f, int d)
{
	struct pan_state *e = f->userdata;
	e->roi_w += d;
	fprintf(stderr, "ROI %d\n", e->roi_w);
	f->changed = 1;
}


static void action_move_roi(struct FTR *f, int x, int y)
{
	struct pan_state *e = f->userdata;
	if (e->roi)
	{
		e->roi_x = x - e->roi_w / 2;
		e->roi_y = y - e->roi_w / 2;
		f->changed = 1;
	}
}

static void action_center_contrast_at_point(struct FTR *f, int x, int y)
{
	struct pan_state *e = f->userdata;

	double p[2];
	window_to_image(p, e, x, y);
	float c[3];
	pixel(c, e, p[0], p[1]);
	float C = (c[0] + c[1] + c[2])/3;

	e->bbb[0] = 127.5 - e->a * c[0];
	e->bbb[1] = 127.5 - e->a * c[1];
	e->bbb[2] = 127.5 - e->a * c[2];

	e->b = 127.5 - e->a * C;

	f->changed = 1;
}

static void action_contrast_span(struct FTR *f, float factor)
{
	struct pan_state *e = f->userdata;

	float c = (127.5 - e->b)/ e->a;
	float ccc[3];
	for(int l=0;l<3;l++) ccc[l] = (127.5 - e->bbb[l]) / e->a;
	e->a *= factor;
	e->b = 127.5 - e->a * c;
	for(int l=0;l<3;l++) e->bbb[l] = 127.5 - e->a * ccc[l];

	f->changed = 1;
}

static void action_change_zoom_by_factor(struct FTR *f, int x, int y, double F)
{
	struct pan_state *e = f->userdata;

	double c[2];
	window_to_image(c, e, x, y);

	e->zoom_factor *= F;
	e->offset_x = c[0] - x/e->zoom_factor;
	e->offset_y = c[1] - y/e->zoom_factor;
	fprintf(stderr, "\t zoom changed %g\n", e->zoom_factor);

	f->changed = 1;
}

static void action_reset_zoom_only(struct FTR *f, int x, int y)
{
	struct pan_state *e = f->userdata;

	action_change_zoom_by_factor(f, x, y, 1/e->zoom_factor);
}



static void action_increase_zoom(struct FTR *f, int x, int y)
{
	action_change_zoom_by_factor(f, x, y, WHEEL_FACTOR);
}

static void action_decrease_zoom(struct FTR *f, int x, int y)
{
	action_change_zoom_by_factor(f, x, y, 1.0/WHEEL_FACTOR);
}

static bool insideP(int w, int h, int i, int j)
{
	return i>=0 && j>=0 && i<w && j<h;
}

#define OMIT_BLUR_MAIN
#include "blur.c"

#define OMIT_PPSMOOTH_MAIN
#include "ppsmooth.c"

static void ppsmooth_vec(float *y, float *x, int w, int h, int pd)
{
	float *x_split = malloc(w*h*pd*sizeof*x);
	float *y_split = malloc(w*h*pd*sizeof*x);

	for (int j = 0; j < h; j++)
	for (int i = 0; i < w; i++)
	for (int l = 0; l < pd; l++)
		x_split[l*w*h+(j*w+i)] = x[pd*(j*w+i)+l];

	ppsmooth_split(y_split, x_split, w, h, pd);

	for (int j = 0; j < h; j++)
	for (int i = 0; i < w; i++)
	for (int l = 0; l < pd; l++)
		y[pd*(j*w+i)+l] = y_split[l*w*h+(j*w+i)];

	free(x_split);
	free(y_split);
}

// note: assumes 3-dimensional pixels
static void transform_roi_buffers_old(float *y, float *x, int n)
{
	for (int j = 0; j < n; j++)
	for (int i = 0; i < n; i++)
	{
		float *xx = x + 3*(j*n + i);
		float *yy = y + 3*(j*n + i);
		float g = hypot(xx[0], hypot(xx[1],xx[2]))/sqrt(3);
		yy[0] = sqrt(g/255)*255;
		yy[1] = g/2;
		yy[2] = g;
	}
	if (false) return;
	float x_p[3*n*n];
	float x_s[3*n*n];
	ppsmooth_vec(x_p, x, n, n, 3);
	for (int i = 0; i < 3*n*n; i++)
		x_s[i] = x[i] - x_p[i];
	float param[1] = {4};
	blur_2d(y, x_p, n, n, 3, "laplace", param, 1);
	for (int i = 0; i < 3*n*n; i++)
		y[i] += x_s[i];
}

static void transform_roi_buffers(float *y, float *x, int n, int roi)
{
	float x_p0[3*n*n], *x_p = x_p0;
	ppsmooth_vec(x_p0, x, n, n, 3);
	if (roi == 2) x_p = x;
	if (roi == 3) { for (int i = 0; i < 3*n*n; i++) y[i]=x_p0[i]; return; }
	float *c = xmalloc(n*n*sizeof*c);
	float *ys = xmalloc(n*n*sizeof*c);
	fftwf_complex *fc = fftwf_xmalloc(n*n*sizeof*fc);
	for (int l = 0; l < 3; l++)
	{
		for (int i = 0; i < n*n; i++)
			c[i] = x_p[3*i+l];
		fft_2dfloat(fc, c, n, n);
		for (int i = 0; i < n*n; i++)
			ys[i] = 255*(log(cabs(fc[i])/255)+0.5)/5;
		for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
		{
			int ii = (i + n/2) % n;
			int jj = (j + n/2) % n;
			y[3*(j*n+i)+l] = ys[jj*n+ii];
		}
	}
	free(ys);
	free(c);
	fftwf_free(fc);
	//float param[1] = {0.5};
	//blur_2d(y, y, n, n, 3, "cauchy", param, 1);
}


// dump the image acording to the state of the viewport
static void pan_exposer(struct FTR *f, int b, int m, int x, int y)
{
	struct pan_state *e = f->userdata;

	// expose the whole image
	for (int j = 0; j < f->h; j++)
	for (int i = 0; i < f->w; i++)
	{
		double p[2];
		window_to_image(p, e, i, j);
		float c[3];
		pixel(c, e, p[0], p[1]);
		//if (!i && !j)
		//	fprintf(stderr, "first pixel (%g %g){%g %g %g}\n",
		//			p[0], p[1], c[0], c[1], c[2]);
		unsigned char *cc = f->rgb + 3 * (j * f->w + i);
		for (int l = 0; l < 3; l++)
		{
			if (!isfinite(c[l]))
				cc[l] = 0;
			else {
				//float g = e->a * c[l] + e->b;
				float g = e->a * c[l] + e->bbb[l];
				if      (g < 0)   cc[l] = 0  ;
				else if (g > 255) cc[l] = 255;
				else              cc[l] = g  ;
			}
		}
	}

	// if pixels are "huge", show their values
	if (e->zoom_factor > 50) // "zoom_factor equals the pixel size"
	{
		// find first inner pixel in the image domain
		double p[2];
		window_to_image(p, e, 0, 0);
		double ip[2] = {ceil(p[0])+0.5, ceil(p[1])+0.5}; // port coords
		int ij[2]; // window coords
		image_to_window(ij, e, ip[0], ip[1]);
		for (int jj=ij[1];jj<f->h-e->zoom_factor;jj+=e->zoom_factor)
		for (int ii=ij[0];ii<f->w-e->zoom_factor;ii+=e->zoom_factor)
		{
			window_to_image(p, e, ii, jj);
			float c[3]; pixel(c, e, p[0], p[1]);
			uint8_t fg[3] = {0, 255, 0};
			uint8_t bg[3] = {0, 0, 0};
			char buf[300];
			int l;
			if (e->zoom_factor>100 && (c[0]!=c[1] || c[0]!=c[2]))
				l=snprintf(buf,300,"%g %g %g",c[0],c[1],c[2]);
			if (c[0] == c[1] && c[0] == c[2])
				l=snprintf(buf, 300, "%g", c[0]);
			put_string_in_rgb_image(f->rgb, f->w, f->h,
					ii-l*e->font->width/2, jj,
					fg, bg, 0, e->font, buf);
		}

	}

	// if ROI, expose the roi
	if (e->roi)
	{
		float buf_in [3 * e->roi_w * e->roi_w];
		float buf_out[3 * e->roi_w * e->roi_w];
		for (int j = 0; j < e->roi_w; j++)
		for (int i = 0; i < e->roi_w; i++)
		{
			// i,h       = position inside ROI
			// ii,jj     = position inside viewport
			// p[0],p[1] = position inside image domain
			int ii = i + e->roi_x - e->roi_w/2;
			int jj = j + e->roi_y - e->roi_w/2;
			double p[2];
			window_to_image(p, e, ii, jj);
			float *c = buf_in + 3 * (j * e->roi_w + i);
			pixel(c, e, p[0], p[1]);
		}
		transform_roi_buffers(buf_out, buf_in, e->roi_w, e->roi);
		for (int j = 0; j < e->roi_w; j++)
		for (int i = 0; i < e->roi_w; i++)
		{
			int ii = i + e->roi_x - e->roi_w/2;
			int jj = j + e->roi_y - e->roi_w/2;
			float *c = buf_out + 3 * (j * e->roi_w + i);
			if (insideP(f->w, f->h, ii, jj))
			{
				uint8_t *cc = f->rgb + 3 * (jj * f->w + ii);
				for (int l = 0; l < 3; l++)
				{
					float g = e->a * c[l] + e->bbb[l];
					if      (g < 0)   cc[l] = 0  ;
					else if (g > 255) cc[l] = 255;
					else              cc[l] = g  ;
					//cc[l] = c[l];
				}
			}
		}
	}

	// mark shit as changed
	f->changed = 1;
}

// update offset variables by dragging
static void pan_motion_handler(struct FTR *f, int b, int m, int x, int y)
{
	//fprintf(stderr, "motion b=%d m=%d (%d %d)\n", b, m, x, y);

	static double ox = 0, oy = 0;

	if (m & FTR_BUTTON_LEFT)   action_offset_viewport(f, x - ox, y - oy);
	if (m & FTR_BUTTON_MIDDLE) action_print_value_under_cursor(f, x, y);
	if (m & FTR_MASK_SHIFT)    action_center_contrast_at_point(f, x, y);

	action_move_roi(f, x, y);

	ox = x;
	oy = y;
}

static void pan_button_handler(struct FTR *f, int b, int m, int x, int y)
{
	//fprintf(stderr, "button b=%d m=%d\n", b, m);
	struct pan_state *e = f->userdata;

	if (e->roi && b == FTR_BUTTON_UP) { action_roi_embiggen(f,+2); return; }
	if (e->roi && b == FTR_BUTTON_DOWN){action_roi_embiggen(f,-2); return; }
	if (b == FTR_BUTTON_UP && m & FTR_MASK_SHIFT) {
		action_contrast_span(f, 1/1.3); return; }
	if (b == FTR_BUTTON_DOWN && m & FTR_MASK_SHIFT) {
		action_contrast_span(f, 1.3); return; }
	if (b == FTR_BUTTON_RIGHT && m & FTR_MASK_CONTROL) {
		action_reset_zoom_only(f, x, y); return; }
	if (b == FTR_BUTTON_MIDDLE) action_print_value_under_cursor(f, x, y);
	if (b == FTR_BUTTON_DOWN)   action_increase_zoom(f, x, y);
	if (b == FTR_BUTTON_UP  )   action_decrease_zoom(f, x, y);
	if (b == FTR_BUTTON_RIGHT)  action_reset_zoom_and_position(f);
}

static void key_handler_print(struct FTR *f, int k, int m, int x, int y)
{
	fprintf(stderr, "key pressed %d '%c' (%d) at %d %d\n",
			k, isalpha(k)?k:' ', m, x, y);
}

static void pan_key_handler(struct FTR *f, int k, int m, int x, int y)
{
	//fprintf(stderr, "PAN_KEY_HANDLER  %d '%c' (%d) at %d %d\n",
	//		k, isalpha(k)?k:' ', m, x, y);

	//if (k == '+') action_increase_zoom(f, f->w/2, f->h/2);
	//if (k == '-') action_decrease_zoom(f, f->w/2, f->h/2);
	if (k == '+') action_change_zoom_by_factor(f, f->w/2, f->h/2, 2);
	if (k == '-') action_change_zoom_by_factor(f, f->w/2, f->h/2, 0.5);
	if (k == 'p') action_change_zoom_by_factor(f, f->w/2, f->h/2, 1.1);
	if (k == 'm') action_change_zoom_by_factor(f, f->w/2, f->h/2, 1/1.1);
	if (k == 'P') action_change_zoom_by_factor(f, f->w/2, f->h/2, 1.006);
	if (k == 'M') action_change_zoom_by_factor(f, f->w/2, f->h/2, 1/1.006);

	//if (k == 'a') action_contrast_change(f, 1.3, 0);
	//if (k == 'A') action_contrast_change(f, 1/1.3, 0);
	//if (k == 'b') action_contrast_change(f, 1, 1);
	//if (k == 'B') action_contrast_change(f, 1, -1);
	if (k == 'n') action_qauto(f);
	if (k == 'r') action_toggle_roi(f, x, y, m&FTR_MASK_SHIFT);

	// if ESC or q, exit
	if  (k == '\033' || k == 'q')
		ftr_notify_the_desire_to_stop_this_loop(f, 1);

	// arrows move the viewport
	if (k > 1000) {
		int d[2] = {0, 0};
		int inc = -10;
		if (m & FTR_MASK_SHIFT  ) inc /= 10;
		if (m & FTR_MASK_CONTROL) inc *= 10;
		switch (k) {
		case FTR_KEY_LEFT : d[0] -= inc; break;
		case FTR_KEY_RIGHT: d[0] += inc; break;
		case FTR_KEY_UP   : d[1] -= inc; break;
		case FTR_KEY_DOWN : d[1] += inc; break;
		}
		if (k == FTR_KEY_PAGE_UP)   d[1] = +f->h/3;
		if (k == FTR_KEY_PAGE_DOWN) d[1] = -f->h/3;
		action_offset_viewport(f, d[0], d[1]);
	}

	// if 'k', do weird things
	if (k == 'k') {
		fprintf(stderr, "setting key_handler_print\n");
		ftr_set_handler(f, "key", key_handler_print);
	}
}


#define BAD_MIN(a,b) a<=b?a:b
int main_cpu(int c, char *v[])
{
	// process input arguments
	if (c != 2 && c != 1) {
		fprintf(stderr, "usage:\n\t%s [image]\n", *v);
		//                          0  1
		return 1;
	}
	char *filename_in = c > 1 ? v[1] : "-";

	// read image
	struct pan_state e[1];
	e->i = fancy_image_open(filename_in, "r");
	e->w = e->i->w;
	e->h = e->i->h;

	// setup font
	e->font[0] = reformat_font(*xfont_6x12, UNPACKED);

	// open window
	struct FTR f = ftr_new_window(BAD_MIN(e->w,1000), BAD_MIN(e->h,800));
	f.userdata = e;
	action_reset_zoom_and_position(&f);
	ftr_set_handler(&f, "expose", pan_exposer);
	ftr_set_handler(&f, "motion", pan_motion_handler);
	ftr_set_handler(&f, "button", pan_button_handler);
	ftr_set_handler(&f, "key"   , pan_key_handler);
	int r = ftr_loop_run(&f);

	// cleanup and exit (optional)
	ftr_close(&f);
	fancy_image_close(e->i);
	return r - 1;
}

int main(int c, char *v[])
{
	return main_cpu(c, v);
}