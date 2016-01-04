/*CosmoSIS interface file for going from shear C(l) to xi+/-
Uses function tpstat_via_hankel from Martin Kilbinger's nicaea
*/
#include "cosmosis/datablock/c_datablock.h"
#include "cosmosis/datablock/section_names.h"
#include "maths.h"

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

const char * shear_xi = SHEAR_XI_SECTION;
const char * shear_cl = SHEAR_CL_SECTION;
const char * wl_nz = WL_NUMBER_DENSITY_SECTION;

typedef enum {shear_shear=0, matter=1, ggl=2} corr_type_t;

typedef struct cl_to_xi_config {
	int n_theta_user;
	double theta_min_user;
	double theta_max_user;
	char * input_section;
	char * output_section;
	char * nz_section_A;
	char * nz_section_B;
	int auto_corr;
	corr_type_t corr_type;

} cl_to_xi_config;


void * setup(c_datablock * options)
{
	cl_to_xi_config * config = malloc(sizeof(cl_to_xi_config));
	int corr_type;
	int status = 0;
	int auto_corr;
	//status |= c_datablock_get_int(options, OPTION_SECTION, "n_theta", 10, &(config->n_theta_user));
	//status |= c_datablock_get_double(options, OPTION_SECTION, "theta_min", 0.0, &(config->theta_min_user));
	//status |= c_datablock_get_double(options, OPTION_SECTION, "theta_max", 0.0, &(config->theta_max_user));
	status |= c_datablock_get_string_default(options, OPTION_SECTION, "input_section_name", "shear_cl", &(config->input_section));
	status |= c_datablock_get_string_default(options, OPTION_SECTION, "output_section_name", "shear_xi", &(config->output_section));
	//optionally read n(z) sections (may be two for cross-correlations)
	status |= c_datablock_get_string_default(options, OPTION_SECTION, "nz_section_name_A", wl_nz, &(config->nz_section_A));
	status |= c_datablock_get_string_default(options, OPTION_SECTION, "nz_section_name_B", config->nz_section_A, &(config->nz_section_B));
	status |= c_datablock_get_int_default(options, OPTION_SECTION, "corr_type", 0, &corr_type);
	//auto_corr tells us whether we have an auto-correlation or cross-correlation.
	status |= c_datablock_get_int_default(options, OPTION_SECTION, "auto_corr", -1, &auto_corr);

	config->corr_type = (corr_type_t)corr_type;

	//If auto_corr is not given in options file, make a sensible choice - set to 0 if
	//nz_section_A and nz_section_B are different, or if corr_type indicates ggl.
	if (auto_corr == -1){
		if (config->nz_section_A != config->nz_section_B){
			auto_corr = 0;
		}
		if (corr_type == 2){
			auto_corr = 0;
		}
	}

	config->auto_corr = auto_corr;

	if (status){
		fprintf(stderr, "Please specify input_section_name and output_section_name in the cl_to_xi module.\n");
		exit(status);
	}

	return config;
}

typedef struct p_projected_data{
	interTable * cl_table;
} p_projected_data;

static double P_projected_loglog(void *info, double ell, int bin_i, int bin_j, error ** err)
{
	p_projected_data * data = (p_projected_data*) info;
	interTable * cl_table = data->cl_table;
	double cl;
	cl =  exp(interpol_wr(cl_table, log(ell), err));
	return cl;
}

static double P_projected_logl(void *info, double ell, int bin_i, int bin_j, error ** err)
{
	p_projected_data * data = (p_projected_data*) info;
	interTable * cl_table = data->cl_table;
	//FILE * f = data->f;
	double cl;
	// if (ell<20.0) cl = 0.0;
	// else if (ell>3000.0) cl = 0.0;
	// else
	cl =  interpol_wr(cl_table, log(ell), err);
	//fprintf(f,"%le  %le\n", ell, cl);
	return cl;
}


int execute(c_datablock * block, void * config_in)
{
	DATABLOCK_STATUS status=0;
	int num_z_bin_A;
	int num_z_bin_B;
	int count=2;
	double ** xi = malloc(count*sizeof(double*));
	for (int i=0; i<count; i++) xi[i] = malloc(sizeof(double)*N_thetaH);

	cl_to_xi_config * config = (cl_to_xi_config*) config_in;

	// Load the number of redshift bins
	status |= c_datablock_get_int(block, config->nz_section_A, "nbin", &num_z_bin_A);
	status |= c_datablock_get_int(block, config->nz_section_B, "nbin", &num_z_bin_B);

	if (status) {
		fprintf(stderr, "Could not load nbin in C_ell -> xi\n");
		return status;
	}
	//Also load ell array
	double * ell;

	int n_ell;
	status |= c_datablock_get_double_array_1d(block, config->input_section, "ell", &ell, &n_ell);
	if (status) {
		fprintf(stderr, "Could not load ell in C_ell -> xi\n");
		return status;
	}
	double log_ell_min = log(ell[0]);
	double log_ell_max = log(ell[n_ell-1]);
	double dlog_ell=(log_ell_max-log_ell_min)/(n_ell-1);
	error * err = NULL;
	interTable* cl_table; //= init_interTable(n_ell, log_ell_min, log_ell_max,
	//	dlog_ell, 1.0, -3.0, &err);

	char name_in[64],name_xip[64],name_xim[64];

	double log_theta_min, log_theta_max;

	// Loop through bin combinations, loading ell,C_ell and computing xi+/-
	int j_bin_start;
  for (int i_bin=1; i_bin<=num_z_bin_A; i_bin++) {
  	if (config->auto_corr==1){
  		j_bin_start=i_bin;
  	}
  	else{
  		j_bin_start=1;
  	}
  	for (int j_bin=j_bin_start; j_bin<=num_z_bin_B; j_bin++) {
			// read in C(l)
			double * C_ell;
			snprintf(name_in, 64, "bin_%d_%d",j_bin,i_bin);
	    	status |= c_datablock_get_double_array_1d(block, config->input_section, name_in, &C_ell, &n_ell);
			if (status) {
				fprintf(stderr, "Could not load bin %d,%d in C_ell -> xi\n", i_bin, j_bin);
				return status;
			}

			// Choose the type of Hankel transform
			tpstat_t tpstat;
			switch(config->corr_type) {
				case shear_shear:
					tpstat = tp_xipm;
					snprintf(name_xip, 64, "xiplus_%d_%d",j_bin,i_bin);
					snprintf(name_xim, 64, "ximinus_%d_%d",j_bin,i_bin);
					break;
				case matter:
					tpstat = tp_w;
					snprintf(name_xip, 64, "wmatter_%d_%d",j_bin,i_bin);
					break;
				case ggl:
					tpstat = tp_gt;
					snprintf(name_xip, 64, "tanshear_%d_%d",j_bin,i_bin);
					break;
				default:
					printf("corr_type: %d\n", config->corr_type);
					printf("ERROR: Invalid corr_type in cl_to_xi_interface\n");
					return 10;
			}

			//check for zeros...


			//fill cl_table for P_projected
			//need to check for zero or negative values...if there are some, can't do loglog interpolation
			int neg_vals=0;
			for (int i=0; i<n_ell; i++){
				if (C_ell[i]<=0) {
					neg_vals += 1;
				}
			}
			//also check for zeros, and replace with v small number if all other C_ells are all +ve or -ve
			for (int i=0; i<n_ell; i++){
				if (fabs(C_ell[i])<=1.e-30) {
					if (neg_vals==n_ell){
						C_ell[i]=-1.e-30;
					}
					else if (neg_vals==0) {
						C_ell[i]=1.e-30;
					}
				}
			}		

			if (neg_vals == 0) {
		    cl_table = init_interTable(n_ell, log_ell_min, log_ell_max,
							       dlog_ell, 1.0, -3.0, &err);
		    for (int i=0; i<n_ell; i++){
			    cl_table->table[i] = log(C_ell[i]);
		    }
		  }
			else if (neg_vals == n_ell){
		  	//In this case all the C(l)s are negative, so interpolate in log(-C)
		  	//just remember to flip sign again at the end
		  	cl_table = init_interTable(n_ell, log_ell_min, log_ell_max,
					     	dlog_ell, 1.0, -3.0, &err);
		  	for (int i=0; i<n_ell; i++){
		    	cl_table->table[i] = log(-C_ell[i]);
		  	}
			}
			else {
				static int warned=0;
		  	if (warned==0){
			  	printf("Negative values in C(l). No interpolation in log(C(l)),\n");
			  	printf("and no power law extrapolation. So make sure range of input ell\n");
			  	printf("is sufficient for this not to matter. \n");
			  	printf("This warning will only appear once per process. \n");
			  	warned=1;
		  	}
		    cl_table = init_interTable(n_ell, log_ell_min, log_ell_max,
							 dlog_ell, 0., 0., &err);
		    for (int i=0; i<n_ell; i++){
			    cl_table->table[i] = C_ell[i];
		    }
			}

    	p_projected_data d;
    	d.cl_table = cl_table;
    	//d.f = f;
			if (neg_vals == 0) {
		    tpstat_via_hankel(&d, xi, &log_theta_min, &log_theta_max,
				       tpstat, &P_projected_loglog, i_bin, j_bin, &err);
			}
			else if (neg_vals == n_ell){
		  	tpstat_via_hankel(&d, xi, &log_theta_min, &log_theta_max,
				    tpstat, &P_projected_loglog, i_bin, j_bin, &err);
		  	double xip_orig,xim_orig;
		  	for (int i=0; i<N_thetaH; i++){
		    	xip_orig = xi[0][i];
					xi[0][i] =-1*xip_orig;
					if (config->corr_type == shear_shear) {
		    		xim_orig = xi[1][i];
		    		xi[1][i] =-1*xim_orig;
					}
		  	}
			}
			else {
		    tpstat_via_hankel(&d, xi, &log_theta_min, &log_theta_max,
				       tpstat, &P_projected_logl, i_bin, j_bin, &err);
			}
			//Now save to block
			c_datablock_put_int(block, config->output_section, "nbin_A", num_z_bin_A);
			c_datablock_put_int(block, config->output_section, "nbin_B", num_z_bin_B);
			c_datablock_put_double_array_1d(block, config->output_section, name_xip,
		                  xi[0], N_thetaH);
			if (config->corr_type == shear_shear) {
				c_datablock_put_double_array_1d(block, config->output_section, name_xim,
                  	xi[1], N_thetaH);
			}
			free(C_ell);
			//fclose(f);
		}
	}
	// Save theta values...check these are being calculated correctly at some point
	double dlog_theta= (log_theta_max-log_theta_min)/((double)N_thetaH-1.0);
	double logtheta_center = 0.5*(log_theta_max+log_theta_min);
	int nc = N_thetaH/2+1;
	double theta_vals[N_thetaH];
	for (int i; i<N_thetaH; i++){
		theta_vals[i] = exp(log_theta_min+i*dlog_theta);
	}
	c_datablock_put_double_array_1d(block, config->output_section, "theta",
                  theta_vals, N_thetaH);
	//Include units
	c_datablock_put_metadata(block, config->output_section, "theta", "unit", "radians");


	//Clean up

	for (int i=0; i<count; i++) free(xi[i]);
	free(xi);

	return status;
}

int cleanup(void * config_in)
{
	// Free the memory that we allocated in the setup
	cl_to_xi_config * config = (cl_to_xi_config*) config_in;
	free(config);
}
