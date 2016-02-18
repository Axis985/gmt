/*--------------------------------------------------------------------
 *	$Id$
 *
 *	Copyright (c) 1991-2015 by P. Wessel, W. H. F. Smith, R. Scharroo, J. Luis and F. Wobbe
 *	See LICENSE.TXT file for copying and redistribution conditions.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU Lesser General Public License as published by
 *	the Free Software Foundation; version 3 or any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU Lesser General Public License for more details.
 *
 *	Contact info: gmt.soest.hawaii.edu
 *--------------------------------------------------------------------*/
/*
 * Author:	Paul Wessel & David Sandwell
 * Date:	1-JAN-2016
 * Version:	5 API
 *
 * Brief synopsis: gpsgridder grids GPS vector strain data u(x,y) & v(x,y) using
 * Green's functions derived from a thin elastic sheet [Haines et al., 2015]
 */

#define THIS_MODULE_NAME	"gpsgridder"
#define THIS_MODULE_LIB		"potential"
#define THIS_MODULE_PURPOSE	"Interpolate GPS strain vectors using Green's functions for elastic deformation"
#define THIS_MODULE_KEYS	"<D{,ND(,TG(,CD),GG},RG-"

#include "gmt_dev.h"

#define GMT_PROG_OPTIONS "-:>RVbdfghinors" GMT_ADD_x_OPT

/* Control structure for gpsgridder */

struct GPSGRIDDER_CTRL {
	struct C {	/* -C[n|v]<cutoff>[/<file>] */
		bool active;
		unsigned int mode;
		double value;
		char *file;
	} C;
	struct G {	/* -G<output_grdfile_template_or_tablefile> */
		bool active;
		char *file;
	} G;
	struct I {	/* -Idx[/dy] */
		bool active;
		double inc[2];
	} I;
	struct L {	/* -L */
		bool active;
	} L;
	struct N {	/* -N<outputnode_file> */
		bool active;
		char *file;
	} N;
	struct S {	/* -S<nu> */
		bool active;
		double nu;
	} S;
	struct T {	/* -T<mask_grdfile> */
		bool active;
		char *file;
	} T;
	struct W {	/* -W[w] */
		bool active;
		unsigned int mode;
	} W;
};

enum Gpsgridded_enum {	/* Indices for coeff array for normalization */
	GSP_MEAN_X	= 0,
	GSP_MEAN_Y	= 1,
	GSP_MEAN_U	= 2,
	GSP_MEAN_V	= 3,
	GSP_SLP_UX	= 4,
	GSP_SLP_UY	= 5,
	GSP_SLP_VX	= 6,
	GSP_SLP_VY	= 7,
	GSP_RANGE_U	= 8,
	GSP_RANGE_V	= 9,
	GSP_LENGTH	= 10,
	GMT_U		= 2,	/* Index into input/output rows */
	GMT_V		= 3,
	GMT_WU		= 2,	/* Index into X row vector with x,y[,du,dv] */
	GMT_WV		= 3,
	GPS_TREND	= 1,	/* Remove/Restore linear trend */
	GPS_NORM	= 2	/* Normalize residual data to 0-1 range */
};

void *New_gpsgridder_Ctrl (struct GMT_CTRL *GMT) {	/* Allocate and initialize a new control structure */
	struct GPSGRIDDER_CTRL *C;

	C = GMT_memory (GMT, NULL, 1, struct GPSGRIDDER_CTRL);

	/* Initialize values whose defaults are not 0/false/NULL */
	C->S.nu = 0.25;	/* Poisson's ratio */
	return (C);
}

void Free_gpsgridder_Ctrl (struct GMT_CTRL *GMT, struct GPSGRIDDER_CTRL *C) {	/* Deallocate control structure */
	if (!C) return;
	gmt_free (C->C.file);
	gmt_free (C->G.file);
	gmt_free (C->N.file);
	gmt_free (C->T.file);
	GMT_free (GMT, C);
}

int GMT_gpsgridder_usage (struct GMTAPI_CTRL *API, int level) {
	GMT_show_name_and_purpose (API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return (GMT_NOERROR);
	GMT_Message (API, GMT_TIME_NONE, "usage: gpsgridder [<table>] -G<outfile>[%s]\n", GMT_Rgeo_OPT);
	GMT_Message (API, GMT_TIME_NONE, "\t[-I<dx>[/<dy>] [-C[n|v]<cut>[/<file>]] [-L] [-N<nodes>] [-S<nu>] [-T<maskgrid>] [%s]\n", GMT_V_OPT);
	GMT_Message (API, GMT_TIME_NONE, "\t[-W[w]] [%s] [%s] [%s]\n\t[%s] [%s]\n\t[%s] [%s] [%s] [%s]%s[%s]\n\n",
		GMT_bi_OPT, GMT_d_OPT, GMT_f_OPT, GMT_h_OPT, GMT_i_OPT, GMT_n_OPT, GMT_o_OPT, GMT_r_OPT, GMT_s_OPT, GMT_x_OPT, GMT_colon_OPT);

	if (level == GMT_SYNOPSIS) return (EXIT_FAILURE);

	GMT_Message (API, GMT_TIME_NONE, "\tChoose one of three ways to specify where to evaluate the spline:\n");
	GMT_Message (API, GMT_TIME_NONE, "\t1. Specify a rectangular grid domain with options -R, -I [and optionally -r].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t2. Supply a mask file via -T whose values are NaN or 0.  The spline will then\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   only be evaluated at the nodes originally set to zero.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t3. Specify a set of output locations via the -N option.\n\n");
	GMT_Message (API, GMT_TIME_NONE, "\t<table> [or stdin] must contain x y u v [weight_u weight_v] records.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Specify -fg to convert longitude, latitude to Flat Earth coordinates.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-G Give name of output file (if -N) or a gridfile name template that must.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   contain the format specifier \"%%s\" which will be replaced with u or v.\n");

	GMT_Message (API, GMT_TIME_NONE, "\n\tOPTIONS:\n");

	GMT_Option (API, "<");
	GMT_Message (API, GMT_TIME_NONE, "\t-C Solve by SVD and eliminate eigenvalues whose ratio to largest eigenvalue is less than <cut> [0].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Optionally append /<filename> to save the eigenvalues to this file.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   A negative cutoff will stop execution after saving the eigenvalues.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Use -Cn to select only the largest <cut> eigenvalues [all].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Use -Cv to select only eigenvalues needed to explain <cut> %% of data variance [all].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   [Default uses Gauss-Jordan elimination to solve the linear system]\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-I Specify a regular set of output locations.  Give equidistant increment for each dimension.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Requires -R for specifying the output domain.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-L Leave trend alone.  Do not remove least squares plane from data before spline fit.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t    [Default removes least squares plane, fits normalized residuals, and restores plane].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-N ASCII file with desired output locations.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   The resulting ASCII coordinates and interpolation are written to file given in -G\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   or stdout if no file specified (see -bo for binary output).\n");
	GMT_Option (API, "R");
	GMT_Message (API, GMT_TIME_NONE, "\t   Requires -I for specifying equidistant increments.  A gridfile may be given;\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   this then also sets -I (and perhaps -r); use those options to override the grid settings.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-S Give effective Poisson's ratio [0.25].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-T Mask grid file whose values are NaN or 0; its header implicitly sets -R, -I (and -r).\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-W Expects two extra input columns with data errors sigma_x, sigma_y).\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Append w to indicate these columns carry weight factors instead.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   [Default makes weights via 1/sigma_x, 1/sigma_y].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Note this will only have an effect if -C is used.\n");
	GMT_Option (API, "V,bi");
	GMT_Message (API, GMT_TIME_NONE, "\t   Default is 4-6 input columns (see -W); use -i to select columns from any data table.\n");
	GMT_Option (API, "d,f,h,i,n,o,r,s,x,:,.");

	return (EXIT_FAILURE);
}

int GMT_gpsgridder_parse (struct GMT_CTRL *GMT, struct GPSGRIDDER_CTRL *Ctrl, struct GMT_OPTION *options) {
	/* This parses the options provided to gpsgridder and sets parameters in CTRL.
	 * Any GMT common options will override values set previously by other commands.
	 * It also replaces any file names specified as input or output with the data ID
	 * returned when registering these sources/destinations with the API.
	 */

	unsigned int n_errors = 0, k;
	struct GMT_OPTION *opt = NULL;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {

			case '<':	/* Skip input files */
				if (!GMT_check_filearg (GMT, '<', opt->arg, GMT_IN, GMT_IS_DATASET)) n_errors++;
				break;

			/* Processes program-specific parameters */

			case 'C':	/* Solve by SVD */
				Ctrl->C.active = true;
				if (opt->arg[0] == 'v') Ctrl->C.mode = 1;
				else if (opt->arg[0] == 'n') Ctrl->C.mode = 2;
				k = (Ctrl->C.mode) ? 1 : 0;
				if (strchr (opt->arg, '/')) {
					char tmp[GMT_BUFSIZ];
					sscanf (&opt->arg[k], "%lf/%s", &Ctrl->C.value, tmp);
					Ctrl->C.file = strdup (tmp);
				}
				else
					Ctrl->C.value = atof (&opt->arg[k]);
				break;
			case 'G':	/* Output file name or grid template */
				Ctrl->G.active = true;
				Ctrl->G.file = strdup (opt->arg);
				break;
			case 'I':	/* Grid spacings */
				Ctrl->I.active = true;
				if (GMT_getinc (GMT, opt->arg, Ctrl->I.inc)) {
					GMT_inc_syntax (GMT, 'I', 1);
					n_errors++;
				}
				break;
			case 'L':	/* Leave trend alone [Default removes LS plane] */
				Ctrl->L.active = true;
				break;
			case 'N':	/* Discrete output locations, no grid will be written */
				if ((Ctrl->N.active = GMT_check_filearg (GMT, 'N', opt->arg, GMT_IN, GMT_IS_DATASET)) != 0)
					Ctrl->N.file = strdup (opt->arg);
				else
					n_errors++;
				break;
			case 'S':	/* Poission's ratio */
				Ctrl->S.nu = atof (opt->arg);
				break;
			case 'T':	/* Input mask grid */
				if ((Ctrl->T.active = GMT_check_filearg (GMT, 'T', opt->arg, GMT_IN, GMT_IS_GRID)) != 0) {	/* Obtain -R -I -r from file */
					struct GMT_GRID *G = NULL;
					Ctrl->T.file = strdup (opt->arg);
					if ((G = GMT_Read_Data (API, GMT_IS_GRID, GMT_IS_FILE, GMT_IS_SURFACE, GMT_GRID_HEADER_ONLY, NULL, opt->arg, NULL)) == NULL) {	/* Get header only */
						return (API->error);
					}
					GMT_memcpy (GMT->common.R.wesn, G->header->wesn, 4, double);
					GMT_memcpy (Ctrl->I.inc, G->header->inc, 2, double);
					GMT->common.r.registration = G->header->registration;
					if (GMT_Destroy_Data (API, &G) != GMT_OK) {
						return (API->error);
					}
					GMT->common.R.active = true;
				}
				else
					n_errors++;
				break;
			case 'W':	/* Expect data weights in last two columns */
				Ctrl->W.active = true;
				break;
			default:	/* Report bad options */
				n_errors += GMT_default_error (GMT, opt->option);
				break;
		}
	}

	n_errors += GMT_check_condition (GMT, !(GMT->common.R.active || Ctrl->N.active || Ctrl->T.active), "Syntax error: No output locations specified (use either [-R -I], -N, or -T)\n");
	n_errors += GMT_check_binary_io (GMT, 4 + 2*Ctrl->W.active);
	n_errors += GMT_check_condition (GMT, Ctrl->C.active && Ctrl->C.value < 0.0 && !Ctrl->C.file, "Syntax error -C option: Must specify file name for eigenvalues if cut < 0\n");
	n_errors += GMT_check_condition (GMT, Ctrl->C.active && Ctrl->C.mode == 1 && Ctrl->C.value > 100.0, "Syntax error -Cv option: Variance explain cannot exceed 100%%\n");
	n_errors += GMT_check_condition (GMT, Ctrl->T.active && !Ctrl->T.file, "Syntax error -T option: Must specify mask grid file name\n");
	n_errors += GMT_check_condition (GMT, Ctrl->N.active && !Ctrl->N.file, "Syntax error -N option: Must specify node file name\n");
	n_errors += GMT_check_condition (GMT, Ctrl->N.active && Ctrl->N.file && GMT_access (GMT, Ctrl->N.file, R_OK), "Syntax error -N: Cannot read file %s!\n", Ctrl->N.file);
	n_errors += GMT_check_condition (GMT, Ctrl->N.file == NULL && !strchr (Ctrl->G.file, '%'), "Syntax error -G option: Must specify a template file name containing %%s\n");
	n_errors += GMT_check_condition (GMT, (Ctrl->I.active + GMT->common.R.active) == 1, "Syntax error: Must specify -R, -I, [-r], -G for gridding\n");

	return (n_errors ? GMT_PARSE_ERROR : GMT_OK);
}

/* GENERAL NUMERICAL FUNCTIONS */

/* Normalization parameters are stored in the coeff array which holds up to GSP_LENGTH terms:
 * coeff[GSP_MEAN_X]:	The mean x coordinate
 * coeff[GSP_MEAN_Y]:	The mean y coordinate
 * coeff[GSP_MEAN_U]:	The mean u observation
 * coeff[GSP_MEAN_V]:	The mean v observation
 * coeff[GSP_SLP_UX]:	The linear x-slope for u
 * coeff[GSP_SLP_UY]:	The linear y-slope for u
 * coeff[GSP_SLP_VX]:	The linear x-slope for v
 * coeff[GSP_SLP_VY]:	The linear y-slope for v
 * coeff[GSP_RANGE_U]:	The largest |range| of the detrended u data
 * coeff[GSP_RANGE_V]:	The largest |range| of the detrended v data
 */

void do_gps_normalization (struct GMTAPI_CTRL *API, double **X, double *u, double *v, uint64_t n, unsigned int mode, double *coeff) {
	/* We always remove/restore the mean observation values.  mode is a combination of bitflags that affects what we do:
	 * Bit GPS_TREND will also remove linear trend
	 * Bit GPS_NORM will normalize residuals by full range
	 */

	uint64_t i;
	double d, umin = DBL_MAX, vmin = DBL_MAX, umax = -DBL_MAX, vmax = -DBL_MAX;

	GMT_Report (API, GMT_MSG_LONG_VERBOSE, "Normalization mode: %d\n", mode);
	GMT_memset (coeff, GSP_LENGTH, double);
	for (i = 0; i < n; i++) {	/* Find mean u and v-values */
		coeff[GSP_MEAN_U] += u[i];
		coeff[GSP_MEAN_V] += v[i];
		if ((mode & GPS_TREND) == 0) continue;	/* Else we also sum up x and y to get their means */
		coeff[GSP_MEAN_X] += X[i][GMT_X];
		coeff[GSP_MEAN_Y] += X[i][GMT_Y];
	}
	coeff[GSP_MEAN_U] /= n;	/* Average u value to remove/restore */
	coeff[GSP_MEAN_V] /= n;	/* Average v value to remove/restore */

	if (mode & GPS_TREND) {	/* Solve for LS plane using deviations from mean x,y,u,v */
		double xx, yy, uu, vv, sxx, sxy, sxu, sxv, syy, syu, syv;
		sxx = sxy = sxu = sxv = syy = syu = syv = 0.0;
		coeff[GSP_MEAN_X] /= n;	/* Mean x */
		coeff[GSP_MEAN_Y] /= n;	/* Mean y */
		for (i = 0; i < n; i++) {
			xx = X[i][GMT_X] - coeff[GSP_MEAN_X];
			yy = X[i][GMT_Y] - coeff[GSP_MEAN_Y];
			uu = u[i] - coeff[GSP_MEAN_U];
			vv = v[i] - coeff[GSP_MEAN_V];
			/* xx,yy,uu,vv are residuals relative to the mean values */
			sxx += (xx * xx);
			sxu += (xx * uu);
			sxv += (xx * vv);
			sxy += (xx * yy);
			syy += (yy * yy);
			syu += (yy * uu);
			syv += (yy * vv);
		}

		d = sxx*syy - sxy*sxy;
		if (d != 0.0) {
			coeff[GSP_SLP_UX] = (sxu*syy - sxy*syu)/d;
			coeff[GSP_SLP_UY] = (sxx*syu - sxy*sxu)/d;
			coeff[GSP_SLP_VX] = (sxv*syy - sxy*syv)/d;
			coeff[GSP_SLP_VY] = (sxx*syv - sxy*sxv)/d;
		}
	}

	/* Remove planes (or just means) */

	for (i = 0; i < n; i++) {	/* Also find min/max or residuals in the process */
		u[i] -= coeff[GSP_MEAN_U];	/* Always remove mean u value */
		v[i] -= coeff[GSP_MEAN_V];	/* Always remove mean v value */
		if (mode & GPS_TREND) {	/* Also remove planar trend */
			u[i] -= (coeff[GSP_SLP_UX] * (X[i][GMT_X] - coeff[GSP_MEAN_X]) + coeff[GSP_SLP_UY] * (X[i][GMT_Y] - coeff[GSP_MEAN_Y]));
			v[i] -= (coeff[GSP_SLP_VX] * (X[i][GMT_X] - coeff[GSP_MEAN_X]) + coeff[GSP_SLP_VY] * (X[i][GMT_Y] - coeff[GSP_MEAN_Y]));
		}
		/* Find adjusted min/max for u and v */
		if (u[i] < umin) umin = u[i];
		if (u[i] > umax) umax = u[i];
		if (v[i] < vmin) vmin = v[i];
		if (v[i] > vmax) vmax = v[i];
	}
	if (mode & GPS_NORM) {	/* Normalize by u,v ranges */
		double du, dv;
		coeff[GSP_RANGE_U] = MAX (fabs(umin), fabs(umax));	/* Determine u range */
		coeff[GSP_RANGE_V] = MAX (fabs(vmin), fabs(vmax));	/* Determine v range */
		du = (coeff[GSP_RANGE_U] == 0.0) ? 1.0 : 1.0 / coeff[GSP_RANGE_U];
		dv = (coeff[GSP_RANGE_V] == 0.0) ? 1.0 : 1.0 / coeff[GSP_RANGE_V];
		for (i = 0; i < n; i++) {	/* Normalize 0-1 */
			u[i] *= du;
			v[i] *= dv;
		}
	}

	/* Recover u(x,y) = u[i] * coeff[GSP_RANGE_U] + coeff[GSP_MEAN_U] + coeff[GSP_SLP_UX]*(x-coeff[GSP_MEAN_X]) + coeff[GSP_SLP_UY]*(y-coeff[GSP_MEAN_Y]) */
	GMT_Report (API, GMT_MSG_LONG_VERBOSE, "2-D Normalization coefficients: uoff = %g uxslope = %g xmean = %g uyslope = %g ymean = %g urange = %g\n",
		coeff[GSP_MEAN_U], coeff[GSP_SLP_UX], coeff[GSP_MEAN_X], coeff[GSP_SLP_UY], coeff[GSP_MEAN_Y], coeff[GSP_RANGE_U]);
	/* Recover v(x,y) = v[i] * coeff[GSP_RANGE_V] + coeff[GSP_MEAN_V] + coeff[GSP_SLP_VX]*(x-coeff[GSP_MEAN_X]) + coeff[GSP_SLP_VY]*(y-coeff[GSP_MEAN_Y]) */
	GMT_Report (API, GMT_MSG_LONG_VERBOSE, "2-D Normalization coefficients: voff = %g vxslope = %g xmean = %g vyslope = %g ymean = %g vrange = %g\n",
		coeff[GSP_MEAN_V], coeff[GSP_SLP_VX], coeff[GSP_MEAN_X], coeff[GSP_SLP_VY], coeff[GSP_MEAN_Y], coeff[GSP_RANGE_V]);
}

void undo_gps_normalization (double *X, unsigned int mode, double *coeff) {
 	/* Here, X holds x,y,u,v */
	if (mode & GPS_NORM) {	/* Scale back up by residual data range (if we normalized) */
		X[GMT_U] *= coeff[GSP_RANGE_U];
		X[GMT_V] *= coeff[GSP_RANGE_V];
	}
	/* Add in mean data value plus minimum residual value (if we normalized by range) */
	X[GMT_U] += coeff[GSP_MEAN_U];	
	X[GMT_V] += coeff[GSP_MEAN_V];	
	if (mode & GPS_TREND) {					/* Restore residual trend */
		X[GMT_U] += coeff[GSP_SLP_UX] * (X[GMT_X] - coeff[GSP_MEAN_X]) + coeff[GSP_SLP_UY] * (X[GMT_Y] - coeff[GSP_MEAN_Y]);
		X[GMT_V] += coeff[GSP_SLP_VX] * (X[GMT_X] - coeff[GSP_MEAN_X]) + coeff[GSP_SLP_VY] * (X[GMT_Y] - coeff[GSP_MEAN_Y]);
	}
}

double get_gps_radius (struct GMT_CTRL *GMT, double *X0, double *X1) {
	double r = 0.0;
	/* Get distance between the two points */
	/* 2-D Cartesian or spherical surface in meters */
	r = GMT_distance (GMT, X0[GMT_X], X0[GMT_Y], X1[GMT_X], X1[GMT_Y]);
	return (r);
}

void get_gps_dxdy (struct GMT_CTRL *GMT, double *X0, double *X1, double *dx, double *dy, bool geo) {
	/* Get increments dx,dy between point 1 and 0, as measured from point 1 */
	if (geo) {	/* Do flat Earth approximation in meters */
		double dlon;
		GMT_set_delta_lon (X0[GMT_X], X1[GMT_X], dlon);
		*dx = dlon * cosd (0.5 * (X1[GMT_Y] + X0[GMT_Y])) * GMT->current.proj.DIST_M_PR_DEG;
		*dy = (X1[GMT_Y] - X0[GMT_Y]) * GMT->current.proj.DIST_M_PR_DEG;
	}
	else {	/* Cartesian data */
		*dx = X1[GMT_X] - X0[GMT_X];
		*dy = X1[GMT_Y] - X0[GMT_Y];
	}
}

void evaluate_greensfunctions (double dx, double dy, double par[], double G[]) {
	/* Evaluate the Green's functions q(x), p(x), and w(x), here placed in G[0], G[1], and G[2].
	 * Here, par[0] holds -(2*e+1)/2 and par[1] holds delta_r to prevent singularity */
	
	double dx2 = dx * dx, dy2 = dy * dy;	/* Squared offsets */
	double dr2 = dx2 + dy2 + par[1];			/* Radius squared */
	
	G[0] = G[1] = par[0] * log (dr2);
	dr2 = 1.0 / dr2;	/* Get inverse squared radius */
	G[0] += dx2 * dr2;
	G[1] += dy2 * dr2;
	G[2] = dx * dy * dr2;
}

#define bailout(code) {GMT_Free_Options (mode); return (code);}
#define Return(code) {Free_gpsgridder_Ctrl (GMT, Ctrl); GMT_end_module (GMT, GMT_cpy); bailout (code);}

int GMT_gpsgridder (void *V_API, int mode, void *args) {
	uint64_t col, row, n_read, p, k, i, j, seg, n, n2, n_ok = 0, ij;
	uint64_t Gu_ij, Gv_ij, Guv_ij, Gvu_ij, n_duplicates = 0, n_skip = 0;
	unsigned int normalize, unit = 0, n_cols;
	size_t old_n_alloc, n_alloc;
	int error, out_ID;
	bool geo, skip;

	char *mem_unit[3] = {"kb", "Mb", "Gb"}, *comp[2] = {"u(x,y)", "v(x,y)"}, *tag[2] = {"u", "v"};

	double **X = NULL, *A = NULL, *u = NULL, *v = NULL, *obs = NULL;
	double *alpha_x = NULL, *alpha_y = NULL, *in = NULL;
	double mem, r, dx, dy, par[2], norm[GSP_LENGTH], weight_u, weight_v, weight_ju, weight_jv, r_min, r_max, G[3];

#ifdef DUMPING
	FILE *fp = NULL;
#endif
	struct GMT_GRID *Grid = NULL, *Out[2] = {NULL, NULL};
	struct GMT_DATATABLE *T = NULL;
	struct GMT_DATASET *Nin = NULL;
	struct GMT_GRID_INFO info;
	struct GPSGRIDDER_CTRL *Ctrl = NULL;
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct GMTAPI_CTRL *API = GMT_get_API_ptr (V_API);	/* Cast from void to GMTAPI_CTRL pointer */

	/*----------------------- Standard module initialization and parsing ----------------------*/

	if (API == NULL) return (GMT_NOT_A_SESSION);
	if (mode == GMT_MODULE_PURPOSE) return (GMT_gpsgridder_usage (API, GMT_MODULE_PURPOSE));	/* Return the purpose of program */
	options = GMT_Create_Options (API, mode, args);	if (API->error) return (API->error);	/* Set or get option list */

	if (!options || options->option == GMT_OPT_USAGE) bailout (GMT_gpsgridder_usage (API, GMT_USAGE));	/* Return the usage message */
	if (options->option == GMT_OPT_SYNOPSIS) bailout (GMT_gpsgridder_usage (API, GMT_SYNOPSIS));	/* Return the synopsis */

	/* Parse the command-line arguments */

	GMT = GMT_begin_module (API, THIS_MODULE_LIB, THIS_MODULE_NAME, &GMT_cpy); /* Save current state */
	if (GMT_Parse_Common (API, GMT_PROG_OPTIONS, options)) Return (API->error);
	Ctrl = New_gpsgridder_Ctrl (GMT);	/* Allocate and initialize a new control structure */
	if ((error = GMT_gpsgridder_parse (GMT, Ctrl, options)) != 0) Return (error);

	/*---------------------------- This is the gpsgridder main code ----------------------------*/

	GMT_Report (GMT->parent, GMT_MSG_NORMAL, "gpsgridder IS NOT A WORKING MODULE YET!\n");

	GMT_enable_threads (GMT);	/* Set number of active threads, if supported */
	GMT_Report (API, GMT_MSG_VERBOSE, "Processing input table data\n");
	GMT_memset (norm, GSP_LENGTH, double);
	GMT_memset (&info, 1, struct GMT_GRID_INFO);

	geo = GMT_is_geographic (GMT, GMT_IN);
	if (GMT_is_geographic (GMT, GMT_IN)) {	/* Set pointers to 2-D distance functions */
		GMT_set_geographic (GMT, GMT_IN);
		GMT_set_geographic (GMT, GMT_OUT);
		GMT_init_distaz (GMT, 'k', GMT_FLATEARTH, GMT_MAP_DIST);
		normalize = GPS_TREND + GPS_NORM;
	}
	else {
		GMT_init_distaz (GMT, 'X', 0, GMT_MAP_DIST);
		normalize = GPS_TREND + GPS_NORM;
	}

	if (Ctrl->L.active)
		normalize = GPS_NORM;	/* Do not de-plane, just remove mean and normalize */

	/* Now we are ready to take on some input values */

	if (GMT_Init_IO (API, GMT_IS_DATASET, GMT_IS_POINT, GMT_IN, GMT_ADD_DEFAULT, 0, options) != GMT_OK) {	/* Establishes data input */
		Return (API->error);
	}
	if (GMT_Begin_IO (API, GMT_IS_DATASET, GMT_IN, GMT_HEADER_ON) != GMT_OK) {	/* Enables data input and sets access mode */
		Return (API->error);
	}

	n_alloc = GMT_INITIAL_MEM_ROW_ALLOC;
	X = GMT_memory (GMT, NULL, n_alloc, double *);
	u = GMT_memory (GMT, NULL, n_alloc, double);
	v = GMT_memory (GMT, NULL, n_alloc, double);
	n_cols = (Ctrl->W.active) ? 4 : 2;	/* So X[k][2-3] will have the x,y weights, if -W is active */
	for (k = 0; k < n_alloc; k++) X[k] = GMT_memory (GMT, NULL, n_cols, double);

	GMT_Report (API, GMT_MSG_VERBOSE, "Read input data and check for data constraint duplicates\n");
	n = n_read = 0;
	r_min = DBL_MAX;	r_max = -DBL_MAX;
	do {	/* Keep returning records until we reach EOF */
		if ((in = GMT_Get_Record (API, GMT_READ_DOUBLE, NULL)) == NULL) {	/* Read next record, get NULL if special case */
			if (GMT_REC_IS_ERROR (GMT)) 		/* Bail if there are any read errors */
				Return (GMT_RUNTIME_ERROR);
			if (GMT_REC_IS_ANY_HEADER (GMT)) 	/* Skip all table and segment headers */
				continue;
			if (GMT_REC_IS_EOF (GMT)) 		/* Reached end of file */
				break;
		}

		/* Data record to process */

		if (geo) {	/* Ensure geographic longitudes fit the range since the normalization function expects it */
			if (in[GMT_X] < GMT->common.R.wesn[XLO] && (in[GMT_X] + 360.0) < GMT->common.R.wesn[XHI]) in[GMT_X] += 360.0;
			else if (in[GMT_X] > GMT->common.R.wesn[XHI] && (in[GMT_X] - 360.0) > GMT->common.R.wesn[XLO]) in[GMT_X] -= 360.0;
		}

		X[n][GMT_X] = in[GMT_X];	/* Save x,y  */
		X[n][GMT_Y] = in[GMT_Y];
		/* Check for data duplicates */
		skip = false;
		for (i = 0; !skip && i < n; i++) {
			r = get_gps_radius (GMT, X[i], X[n]);
			if (GMT_IS_ZERO (r)) {	/* Duplicates will give zero point separation */
				if (doubleAlmostEqualZero (in[GMT_U], u[i]) && doubleAlmostEqualZero (in[GMT_V], v[i])) {
					GMT_Report (API, GMT_MSG_NORMAL, "Data constraint %" PRIu64 " is identical to %" PRIu64 " and will be skipped\n", n_read, i);
					skip = true;
					n_skip++;
				}
				else {
					GMT_Report (API, GMT_MSG_NORMAL, "Data constraint %" PRIu64 " and %" PRIu64 " occupy the same location but differ in observation (%.12g/%.12g vs %.12g/%.12g)\n", n_read, i, in[GMT_U], u[i], in[GMT_V], v[i]);
					n_duplicates++;
				}
			}
			else {
				if (r < r_min) r_min = r;
				if (r > r_max) r_max = r;
			}
		}
		n_read++;
		if (skip) continue;	/* Current point was a duplicate of a previous point */
		u[n] = in[GMT_U];	v[n] = in[GMT_V];	/* Save current u,v data pair */
		if (Ctrl->W.active) {	/* Got sigmas or weights */
			X[n][GMT_WU] = in[4];
			X[n][GMT_WV] = in[5];
			if (Ctrl->W.mode == 0) {	/* Got sigmas, create weights */
				X[n][GMT_WU] = 1.0 / X[n][GMT_WU];
				X[n][GMT_WV] = 1.0 / X[n][GMT_WV];
			}
		}
		n++;			/* Added a new data constraint */
		if (n == n_alloc) {	/* Get more memory */
			old_n_alloc = n_alloc;
			n_alloc <<= 1;
			X = GMT_memory (GMT, X, n_alloc, double *);
			u = GMT_memory (GMT, u, n_alloc, double);
			v = GMT_memory (GMT, v, n_alloc, double);
			for (k = old_n_alloc; k < n_alloc; k++) X[k] = GMT_memory (GMT, X[k], n_cols, double);
		}
	} while (true);

	if (GMT_End_IO (API, GMT_IN, 0) != GMT_OK) {	/* Disables further data input */
		Return (API->error);
	}

	n2 = 2 * n;	/* Dimension of array is twice since using u & v */
	for (k = n; k < n_alloc; k++) GMT_free (GMT, X[k]);	/* Remove what was not used */
	X = GMT_memory (GMT, X, n, double *);	/* Realloc to exact size */
	u = GMT_memory (GMT, u, n2, double *);	/* We will append v to the end of u later so need the extra space */
	v = GMT_memory (GMT, v, n, double *);
	GMT_Report (API, GMT_MSG_VERBOSE, "Found %" PRIu64 " unique data constraints\n", n);
	if (n_skip) GMT_Report (API, GMT_MSG_VERBOSE, "Skipped %" PRIu64 " data constraints as duplicates\n", n_skip);

	/* Check for duplicates which would result in a singular matrix system; also update min/max radius */

	GMT_Report (API, GMT_MSG_VERBOSE, "Distance between closest constraints = %.12g]\n", r_min);
	GMT_Report (API, GMT_MSG_VERBOSE, "Distance between distant constraints = %.12g]\n", r_max);

	if (n_duplicates) {	/* These differ in observation value so need to be averaged, medianed, or whatever first */
		GMT_Report (API, GMT_MSG_VERBOSE, "Found %" PRIu64 " data constraint duplicates with different observation values\n", n_duplicates);
		if (!Ctrl->C.active || GMT_IS_ZERO (Ctrl->C.value)) {
			GMT_Report (API, GMT_MSG_VERBOSE, "You must reconcile duplicates before running gpsgridder since they will result in a singular matrix\n");
			for (p = 0; p < n; p++) GMT_free (GMT, X[p]);
			GMT_free (GMT, X);
			GMT_free (GMT, u);
			GMT_free (GMT, v);
			Return (GMT_DATA_READ_ERROR);
		}
		else
			GMT_Report (API, GMT_MSG_VERBOSE, "Expect some eigenvalues to be identically zero\n");
	}

	GMT_Report (API, GMT_MSG_VERBOSE, "Found %" PRIu64 " (u,v) pairs, yielding a %" PRIu64 " by %" PRIu64 " set of linear equations\n", n, n2, n2);

	if (Ctrl->T.file) {	/* Existing grid that will have zeros and NaNs, only */
		if ((Grid = GMT_Read_Data (API, GMT_IS_GRID, GMT_IS_FILE, GMT_IS_SURFACE, GMT_GRID_HEADER_ONLY, NULL, Ctrl->T.file, NULL)) == NULL) {	/* Get header only */
			Return (API->error);
		}
		if (! (Grid->header->wesn[XLO] == GMT->common.R.wesn[XLO] && Grid->header->wesn[XHI] == GMT->common.R.wesn[XHI] && Grid->header->wesn[YLO] == GMT->common.R.wesn[YLO] && Grid->header->wesn[YHI] == GMT->common.R.wesn[YHI])) {
			GMT_Report (API, GMT_MSG_NORMAL, "Error: The mask grid does not match your specified region\n");
			Return (EXIT_FAILURE);
		}
		if (! (Grid->header->inc[GMT_X] == Ctrl->I.inc[GMT_X] && Grid->header->inc[GMT_Y] == Ctrl->I.inc[GMT_Y])) {
			GMT_Report (API, GMT_MSG_NORMAL, "Error: The mask grid resolution does not match your specified grid spacing\n");
			Return (EXIT_FAILURE);
		}
		if (! (Grid->header->registration == GMT->common.r.registration)) {
			GMT_Report (API, GMT_MSG_NORMAL, "Error: The mask grid registration does not match your specified grid registration\n");
			Return (EXIT_FAILURE);
		}
		if (GMT_Read_Data (API, GMT_IS_GRID, GMT_IS_FILE, GMT_IS_SURFACE, GMT_GRID_DATA_ONLY, NULL, Ctrl->T.file, Grid) == NULL) {	/* Get data */
			Return (API->error);
		}
		(void)GMT_set_outgrid (GMT, Ctrl->T.file, Grid, &Out[GMT_X]);	/* true if input is a read-only array; otherwise Out[GMT_X] is just a pointer to Grid */
		n_ok = Out[GMT_X]->header->nm;
		GMT_grd_loop (GMT, Out[GMT_X], row, col, k) if (GMT_is_fnan (Out[GMT_X]->data[k])) n_ok--;
		/* Duplicate to get grid for v */
		if ((Out[GMT_Y] = GMT_Duplicate_Data (API, GMT_IS_GRID, GMT_DUPLICATE_DATA, Out[GMT_X])) == NULL) {
			Return (API->error);
		}
	}
	else if (Ctrl->N.active) {	/* Read output locations from file */
		GMT_disable_i_opt (GMT);	/* Do not want any -i to affect the reading from -C,-F,-L files */
		if ((Nin = GMT_Read_Data (API, GMT_IS_DATASET, GMT_IS_FILE, GMT_IS_POINT, GMT_READ_NORMAL, NULL, Ctrl->N.file, NULL)) == NULL) {
			Return (API->error);
		}
		GMT_reenable_i_opt (GMT);	/* Recover settings provided by user (if -i was used at all) */
		T = Nin->table[0];
	}
	else {	/* Fill in an equidistant output table or grid */
		/* Need a full-fledged Grid creation since we are writing it to who knows where */
		for (k = 0; k < 2; k++) {
			if ((Out[k] = GMT_Create_Data (API, GMT_IS_GRID, GMT_IS_SURFACE, GMT_GRID_ALL, NULL, GMT->common.R.wesn, Ctrl->I.inc, \
				GMT->common.r.registration, GMT_NOTSET, NULL)) == NULL) Return (API->error);
		}
		n_ok = Out[GMT_X]->header->nm;
	}

	/* Initialize the Greens function machinery */
	
	par[0] = 0.5 * (2.0 * (1.0 - Ctrl->S.nu)/(1.0 + Ctrl->S.nu) + 1.0);	/* half of 2*epsilon + 1 */
	par[1] = 1e-2 * r_min;		/* Small fudge factor to avoid singularity for r = 0 */
	
	/* Remove mean (or LS plane) from data (we will add it back later) */

	do_gps_normalization (API, X, u, v, n, normalize, norm);

	/* Set up linear system Ax = b */

	mem = ((double)n2 * (double)n2 * (double)sizeof (double)) / 1024.0;	/* In kb */
	unit = 0;
	while (mem > 1024.0 && unit < 2) { mem /= 1024.0; unit++; }	/* Select next unit */
	GMT_Report (API, GMT_MSG_VERBOSE, "Square matrix requires %.1f %s\n", mem, mem_unit[unit]);
	A = GMT_memory (GMT, NULL, n2 * n2, double);

	GMT_Report (API, GMT_MSG_VERBOSE, "Build linear system Ax = b\n");

	weight_u = weight_v = weight_ju = weight_jv = 1.0;
	for (j = 0; j < n; j++) {	/* For each data constraint pair (u,v): j refers to a row */
		if (Ctrl->W.active) {	/* Apply any weights */
			weight_ju = X[j][GMT_WU];
			weight_jv = X[j][GMT_WV];
			u[j] *= weight_ju;
			v[j] *= weight_jv;
		}
		for (i = 0; i < n; i++) {	/* i refers to a column */
			if (Ctrl->W.active) {
				weight_u = weight_ju * X[i][GMT_WU];
				weight_v = weight_jv * X[i][GMT_WV];
			}
			Gu_ij  = j * n2 + i;		/* Index for Gu term */
			Guv_ij = Gu_ij + n;		/* Index for Guv term */
			Gvu_ij = (j+n) * n2 + i;	/* Index for Gvu term */
			Gv_ij  = Gvu_ij + n;		/* Index for Gu term */
			get_gps_dxdy (GMT, X[i], X[j], &dx, &dy, geo);
			evaluate_greensfunctions (dx, dy, par, G);
			A[Gu_ij]  = weight_u * G[0];
			A[Gv_ij]  = weight_v * G[1];
			A[Guv_ij] = weight_u * G[2];
			A[Gvu_ij] = weight_v * G[2];
		}
	}

	GMT_memcpy (&u[n], v, n, double);	/* Place v array at end of u array */
	obs = u;				/* Use obs to refer to this combined u,v array */
	
	if (Ctrl->C.active) {		/* Solve using svd decomposition */
		int n_use, error;
		double *V = NULL, *s = NULL, *b = NULL, eig_max = 0.0, limit;

		GMT_Report (API, GMT_MSG_VERBOSE, "Solve linear equations by SVD\n");
#ifndef HAVE_LAPACK
		GMT_Report (API, GMT_MSG_VERBOSE, "Note: SVD solution without LAPACK will be very very slow.\n");
		GMT_Report (API, GMT_MSG_VERBOSE, "We strongly recommend you install LAPACK and recompile GMT.\n");
#endif
		V = GMT_memory (GMT, NULL, n2 * n2, double);
		s = GMT_memory (GMT, NULL, n2, double);
		if ((error = GMT_svdcmp (GMT, A, (unsigned int)n2, (unsigned int)n2, s, V)) != 0) Return (error);

		if (Ctrl->C.file) {	/* Save the eigen-values for study */
			double *eig = GMT_memory (GMT, NULL, n2, double);
			uint64_t e_dim[4] = {1, 1, n2, 2};
			struct GMT_DATASET *E = NULL;
			GMT_memcpy (eig, s, n2, double);
			if ((E = GMT_Create_Data (API, GMT_IS_DATASET, GMT_IS_NONE, 0, e_dim, NULL, NULL, 0, 0, NULL)) == NULL) {
				GMT_Report (API, GMT_MSG_NORMAL, "Unable to create a data set for saving eigenvalues\n");
				Return (API->error);
			}

			/* Sort eigenvalues into ascending order */
			GMT_sort_array (GMT, eig, n2, GMT_DOUBLE);
			eig_max = eig[n-1];
			for (i = 0, j = n2-1; i < n2; i++, j--) {
				E->table[0]->segment[0]->coord[GMT_X][i] = i + 1.0;	/* Let 1 be x-value of the first eigenvalue */
				E->table[0]->segment[0]->coord[GMT_Y][i] = (Ctrl->C.mode == 1) ? eig[j] : eig[j] / eig_max;
			}
			if (GMT_Write_Data (API, GMT_IS_DATASET, GMT_IS_FILE, GMT_IS_NONE, GMT_WRITE_SET, NULL, Ctrl->C.file, E) != GMT_OK) {
				Return (API->error);
			}
			if (Ctrl->C.mode == 1)
				GMT_Report (API, GMT_MSG_VERBOSE, "Eigen-values saved to %s\n", Ctrl->C.file);
			else
				GMT_Report (API, GMT_MSG_VERBOSE, "Eigen-value ratios s(i)/s(0) saved to %s\n", Ctrl->C.file);
			GMT_free (GMT, eig);

			if (Ctrl->C.value < 0.0) {	/* We are done */
				for (p = 0; p < n; p++) GMT_free (GMT, X[p]);
				GMT_free (GMT, X);
				GMT_free (GMT, s);
				GMT_free (GMT, V);
				GMT_free (GMT, A);
				GMT_free (GMT, u);
				GMT_free (GMT, v);
				for (k = 0; k > 2; k++)
					GMT_free_grid (GMT, &Out[k], true);
				Return (EXIT_SUCCESS);
			}
		}
		b = GMT_memory (GMT, NULL, n2, double);
		GMT_memcpy (b, obs, n2, double);
		limit = Ctrl->C.value;
		n_use = GMT_solve_svd (GMT, A, (unsigned int)n2, (unsigned int)n2, V, s, b, 1U, obs, &limit, Ctrl->C.mode);
		if (n_use == -1) Return (EXIT_FAILURE);
		GMT_Report (API, GMT_MSG_VERBOSE, "[%d of %" PRIu64 " eigen-values used to explain %.2f %% of data variance]\n", n_use, n2, limit);

		GMT_free (GMT, s);
		GMT_free (GMT, V);
		GMT_free (GMT, b);
	}
	else {				/* Gauss-Jordan elimination */
		int error;
		if (GMT_IS_ZERO (r_min)) {
			GMT_Report (API, GMT_MSG_NORMAL, "Your matrix is singular because you have duplicate data constraints\n");
			GMT_Report (API, GMT_MSG_NORMAL, "Preprocess your data with one of the blockm* modules to eliminate them\n");

		}
		GMT_Report (API, GMT_MSG_VERBOSE, "Solve linear equations by Gauss-Jordan elimination\n");
		if ((error = GMT_gaussjordan (GMT, A, (unsigned int)n2, obs)) != 0) {
			GMT_Report (API, GMT_MSG_NORMAL, "You probably have nearly duplicate data constraints\n");
			GMT_Report (API, GMT_MSG_NORMAL, "Preprocess your data with one of the blockm* modules\n");
			Return (error);
		}
	}
	alpha_x = obs;		/* Just a different name since the obs vector now holds the alpha factors */
	alpha_y = &obs[n];	/* Halfway down we get the y alphas */
#ifdef DUMPING
	fp = fopen ("alpha.txt", "w");	/* Save alpha coefficients for debugging purposes */
	for (p = 0; p < n; p++) fprintf (fp, "%g\t%g\n", alpha_x[p], alpha_y[p]);
	fclose (fp);
#endif
	GMT_free (GMT, A);

	if (Ctrl->N.file) {	/* Predict solution at specified discrete points only */
		unsigned int wmode = GMT_ADD_DEFAULT;
		double out[4];

		/* Must register Ctrl->G.file first since we are going to writing rec-by-rec */
		if (Ctrl->G.active) {
			if ((out_ID = GMT_Register_IO (API, GMT_IS_DATASET, GMT_IS_FILE, GMT_IS_POINT, GMT_OUT, NULL, Ctrl->G.file)) == GMT_NOTSET)
				Return (API->error);
			wmode = GMT_ADD_EXISTING;
		}
		if (GMT_Init_IO (API, GMT_IS_DATASET, GMT_IS_POINT, GMT_OUT, wmode, 0, options) != GMT_OK) {	/* Establishes output */
			Return (API->error);
		}
		if (GMT_Begin_IO (API, GMT_IS_DATASET, GMT_OUT, GMT_HEADER_ON) != GMT_OK) {	/* Enables data output and sets access mode */
			Return (API->error);
		}
		if ((error = GMT_set_cols (GMT, GMT_OUT, 4)) != GMT_OK) {
			Return (error);
		}
		GMT_memset (out, 4, double);
		GMT_Report (API, GMT_MSG_VERBOSE, "Evaluate spline at %" PRIu64 " given locations\n", T->n_records);
		/* This cannot be under OpenMP as is since the record writing will appear out of sync.  Must instead
		 * save to memory and THEN write the output via GMT_Write_Data */
		for (seg = 0; seg < T->n_segments; seg++) {
			for (row = 0; row < T->segment[seg]->n_rows; row++) {
				out[GMT_X] = T->segment[seg]->coord[GMT_X][row];
				out[GMT_Y] = T->segment[seg]->coord[GMT_Y][row];
				out[GMT_U] = out[GMT_V] = 0.0;
				for (p = 0; p < n; p++) {
					get_gps_dxdy (GMT, out, X[p], &dx, &dy, geo);
					evaluate_greensfunctions (dx, dy, par, G);
					out[GMT_U] += (alpha_x[p] * G[0] + alpha_y[p] * G[2]);
					out[GMT_V] += (alpha_y[p] * G[1] + alpha_x[p] * G[2]);
				}
				undo_gps_normalization (out, normalize, norm);
				GMT_Put_Record (API, GMT_WRITE_DOUBLE, out);
			}
		}
		if (GMT_End_IO (API, GMT_OUT, 0) != GMT_OK) {	/* Disables further data output */
			Return (API->error);
		}
		if (GMT_Destroy_Data (API, &Nin) != GMT_OK) {
			Return (API->error);
		}
	}
	else {	/* Output on equidistance lattice */
		int64_t col, row, p; /* On Windows 'for' index variables must be signed, so redefine these 3 inside this block only */
		char file[GMT_BUFSIZ] = {""};
		double *xp = NULL, *yp = NULL, V[4];
		GMT_Report (API, GMT_MSG_VERBOSE, "Evaluate spline at %" PRIu64 " equidistant output locations\n", n_ok);
		/* Precalculate coordinates */
		xp = GMT_grd_coord (GMT, Out[GMT_X]->header, GMT_X);
		yp = GMT_grd_coord (GMT, Out[GMT_X]->header, GMT_Y);
		GMT_memset (V, 4, double);
#ifdef _OPENMP
#pragma omp parallel for private(V,row,col,ij,p,dx,dy) shared(yp,Out,xp,X,Ctrl,GMT,alpha_x,alpha_y,norm,n,normalize,geo)
#endif
		for (row = 0; row < Out[GMT_X]->header->ny; row++) {	/* This would be a dummy loop for 1 row if 1-D data */
			V[GMT_Y] = yp[row];
			for (col = 0; col < Out[GMT_X]->header->nx; col++) {	/* This loop is always active for 1,2,3D */
				ij = GMT_IJP (Out[GMT_X]->header, row, col);
				if (GMT_is_fnan (Out[GMT_X]->data[ij])) continue;	/* Only do solution where mask is not NaN */
				V[GMT_X] = xp[col];
				/* Here, V holds the current output coordinates */
				for (p = 0, V[GMT_U] = V[GMT_V] = 0.0; p < (int64_t)n; p++) {
					get_gps_dxdy (GMT, V, X[p], &dx, &dy, geo);
					evaluate_greensfunctions (dx, dy, par, G);
					V[GMT_U] += (alpha_x[p] * G[0] + alpha_y[p] * G[2]);
					V[GMT_V] += (alpha_y[p] * G[1] + alpha_x[p] * G[2]);
				}
				undo_gps_normalization (V, normalize, norm);
				Out[GMT_X]->data[ij] = (float)V[GMT_U];
				Out[GMT_Y]->data[ij] = (float)V[GMT_V];
			}
		}
		for (k = 0; k < 2; k++) {	/* Write the two grids with u(x,y) and v(xy) */
			GMT_grd_init (GMT, Out[k]->header, options, true);
			sprintf (Out[k]->header->remark, "Strain component %s", comp[k]);
			sprintf (file, Ctrl->G.file, tag[k]);
			if (GMT_Set_Comment (API, GMT_IS_GRID, GMT_COMMENT_IS_OPTION | GMT_COMMENT_IS_COMMAND, options, Out[k])) Return (API->error);
			if (GMT_Write_Data (API, GMT_IS_GRID, GMT_IS_FILE, GMT_IS_SURFACE, GMT_GRID_ALL, NULL, file, Out[k]) != GMT_OK) {
				Return (API->error);
			}
		}
		GMT_free (GMT, xp);
		GMT_free (GMT, yp);
	}

	/* Clean up */

	for (p = 0; p < n; p++) GMT_free (GMT, X[p]);
	GMT_free (GMT, X);
	GMT_free (GMT, u);
	GMT_free (GMT, v);

	GMT_Report (API, GMT_MSG_VERBOSE, "Done\n");

	Return (EXIT_SUCCESS);
}