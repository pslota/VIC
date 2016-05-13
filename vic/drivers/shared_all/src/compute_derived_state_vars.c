/******************************************************************************
 * @section DESCRIPTION
 *
 * This routine computes the state variables (energy balance, water balance,
 * and snow components) that are derived from the variables that are stored in
 * state files.
 *
 * @section LICENSE
 *
 * The Variable Infiltration Capacity (VIC) macroscale hydrological model
 * Copyright (C) 2014 The Land Surface Hydrology Group, Department of Civil
 * and Environmental Engineering, University of Washington.
 *
 * The VIC model is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *****************************************************************************/

#include <vic_driver_shared_all.h>

/******************************************************************************
 * @brief    Compute the state variables (energy balance, water balance,
 *           and snow components) that are derived from the variables that
 *           are stored in state files.
 *****************************************************************************/
void
compute_derived_state_vars(all_vars_struct *all_vars,
                           soil_con_struct *soil_con,
                           veg_con_struct  *veg_con)
{
    extern global_param_struct global_param;
    extern option_struct       options;

    char                       FIRST_VEG;
    size_t                     Nveg;
    size_t                     veg;
    size_t                     lidx;
    size_t                     band;
    size_t                     frost_area;
    int                        ErrorFlag;
    double                     Cv;
    double                     moist[MAX_VEG][MAX_BANDS][MAX_LAYERS];
    double                     ice[MAX_VEG][MAX_BANDS][MAX_LAYERS][
        MAX_FROST_AREAS];
    double                     dt_thresh;
    double                     tmp_runoff;

    cell_data_struct         **cell;
    energy_bal_struct        **energy;

    cell = all_vars->cell;
    energy = all_vars->energy;
    Nveg = veg_con[0].vegetat_type_num;

    /******************************************
       Compute derived soil layer vars
    ******************************************/
    for (veg = 0; veg <= Nveg; veg++) {
        // Initialize soil for existing vegetation types
        Cv = veg_con[veg].Cv;

        if (Cv > 0) {
            for (band = 0; band < options.SNOW_BAND; band++) {
                // Initialize soil for existing snow elevation bands
                if (soil_con->AreaFract[band] > 0.) {
                    // set up temporary moist and ice arrays
                    for (lidx = 0; lidx < options.Nlayer; lidx++) {
                        moist[veg][band][lidx] =
                            cell[veg][band].layer[lidx].moist;
                        for (frost_area = 0;
                             frost_area < options.Nfrost;
                             frost_area++) {
                            ice[veg][band][lidx][frost_area] =
                                cell[veg][band].layer[lidx].ice[frost_area];
                        }
                    }

                    // compute saturated area and water table
                    compute_runoff_and_asat(soil_con, moist[veg][band], 0,
                                            &(cell[veg][band].asat),
                                            &tmp_runoff);
                    wrap_compute_zwt(soil_con, &(cell[veg][band]));
                }
            }
        }
    }

    /******************************************
       Compute soil thermal node properties
    ******************************************/

    FIRST_VEG = true;
    for (veg = 0; veg <= Nveg; veg++) {
        // Initialize soil for existing vegetation types
        Cv = veg_con[veg].Cv;

        if (Cv > 0) {
            for (band = 0; band < options.SNOW_BAND; band++) {
                // Initialize soil for existing snow elevation bands
                if (soil_con->AreaFract[band] > 0.) {
                    /** Set soil properties for all soil nodes **/
                    if (FIRST_VEG) {
                        FIRST_VEG = false;
                        set_node_parameters(soil_con->Zsum_node,
                                            soil_con->max_moist_node,
                                            soil_con->expt_node,
                                            soil_con->bubble_node,
                                            soil_con->alpha, soil_con->beta,
                                            soil_con->gamma, soil_con->depth,
                                            soil_con->max_moist, soil_con->expt,
                                            soil_con->bubble,
                                            options.Nnode, options.Nlayer);
                    }

                    // set soil moisture properties for all soil thermal nodes
                    if (options.FULL_ENERGY || options.FROZEN_SOIL) {
                        ErrorFlag =
                            distribute_node_moisture_properties(
                                energy[veg][band].moist,
                                energy[veg][band].ice,
                                energy[veg][band].kappa_node,
                                energy[veg][band].Cs_node,
                                soil_con->Zsum_node,
                                energy[veg][band].T,
                                soil_con->max_moist_node,
                                soil_con->expt_node,
                                soil_con->bubble_node,
                                moist[veg][band],
                                soil_con->depth,
                                soil_con->soil_dens_min,
                                soil_con->bulk_dens_min,
                                soil_con->quartz,
                                soil_con->soil_density,
                                soil_con->bulk_density,
                                soil_con->organic,
                                options.Nnode, options.Nlayer,
                                soil_con->FS_ACTIVE);
                        if (ErrorFlag == ERROR) {
                            log_err("Error setting physical properties for "
                                    "soil thermal nodes");
                        }
                    }

                    // Check node spacing v time step
                    // (note this is only approximate since heat capacity and
                    // conductivity can change considerably during the
                    // simulation depending on soil moisture and ice content)
                    if ((options.FROZEN_SOIL &&
                         !options.QUICK_FLUX) && !options.IMPLICIT) {
                        // in seconds
                        dt_thresh = 0.5 * energy[veg][band].Cs_node[1] /
                                    energy[veg][band].kappa_node[1] *
                                    pow((soil_con->dz_node[1]),
                                        2);
                        if (global_param.dt > dt_thresh) {
                            log_err("You are currently running FROZEN SOIL "
                                    "with an explicit method (IMPLICIT is "
                                    "set to FALSE).  For the explicit method "
                                    "to be stable, time step %f seconds is too "
                                    "large for the given thermal node spacing "
                                    "%f m, soil heat capacity %f J/m3/K, and "
                                    "soil thermal conductivity %f J/m/s/K.  "
                                    "Either set IMPLICIT to TRUE in your "
                                    "global parameter file (this is the "
                                    "recommended action), or decrease time "
                                    "step length to <= %f seconds, or decrease "
                                    "the number of soil thermal nodes.",
                                    global_param.dt,
                                    soil_con->dz_node[1],
                                    energy[veg][band].Cs_node[1],
                                    energy[veg][band].kappa_node[1], dt_thresh);
                        }
                    }

                    /* initialize layer moistures and ice contents */
                    for (lidx = 0; lidx < options.Nlayer; lidx++) {
                        cell[veg][band].layer[lidx].moist =
                            moist[veg][band][lidx];
                        for (frost_area = 0;
                             frost_area < options.Nfrost;
                             frost_area++) {
                            cell[veg][band].layer[lidx].ice[frost_area] =
                                ice[veg][band][lidx][frost_area];
                        }
                    }
                    if (options.QUICK_FLUX) {
                        ErrorFlag =
                            estimate_layer_ice_content_quick_flux(
                                cell[veg][band].layer,
                                soil_con->depth, soil_con->dp,
                                energy[
                                    veg][band].T[0], energy[veg][band].T[1],
                                soil_con->avg_temp, soil_con->max_moist,
                                soil_con->expt, soil_con->bubble,
                                soil_con->frost_fract, soil_con->frost_slope,
                                soil_con->FS_ACTIVE);
                        if (ErrorFlag == ERROR) {
                            log_err("Error in "
                                    "estimate_layer_ice_content_quick_flux");
                        }
                    }
                    else {
                        ErrorFlag = estimate_layer_ice_content(
                            cell[veg][band].layer,
                            soil_con->Zsum_node,
                            energy[veg][band].T,
                            soil_con->depth,
                            soil_con->max_moist,
                            soil_con->expt,
                            soil_con->bubble,
                            soil_con->frost_fract,
                            soil_con->frost_slope,
                            options.Nnode,
                            options.Nlayer,
                            soil_con->FS_ACTIVE);
                        if (ErrorFlag == ERROR) {
                            log_err("Error in "
                                    "estimate_layer_ice_content");
                        }
                    }

                    /* Find freezing and thawing front depths */
                    if (!options.QUICK_FLUX && soil_con->FS_ACTIVE) {
                        find_0_degree_fronts(&energy[veg][band],
                                             soil_con->Zsum_node,
                                             energy[veg][band].T,
                                             options.Nnode);
                    }
                }
            }
        }
    }
}