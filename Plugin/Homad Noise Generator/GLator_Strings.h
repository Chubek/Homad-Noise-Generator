/*******************************************************************/
/*                                                                 */
/*                      ADOBE CONFIDENTIAL                         */
/*                   _ _ _ _ _ _ _ _ _ _ _ _ _                     */
/*                                                                 */
/* Copyright 2007 Adobe Systems Incorporated                       */
/* All Rights Reserved.                                            */
/*                                                                 */
/* NOTICE:  All information contained herein is, and remains the   */
/* property of Adobe Systems Incorporated and its suppliers, if    */
/* any.  The intellectual and technical concepts contained         */
/* herein are proprietary to Adobe Systems Incorporated and its    */
/* suppliers and may be covered by U.S. and Foreign Patents,       */
/* patents in process, and are protected by trade secret or        */
/* copyright law.  Dissemination of this information or            */
/* reproduction of this material is strictly forbidden unless      */
/* prior written permission is obtained from Adobe Systems         */
/* Incorporated.                                                   */
/*                                                                 */
/*******************************************************************/

#pragma once

typedef enum {
	StrID_NONE, 
	StrID_Name,
	StrID_Description,
	StrID_Color_Param_Name,
	StrID_THOR_GENERIC_1D_START_Name,
	StrID_THOR_GENERIC_1D_VALUE_1_Name,
	StrID_THOR_GENERIC_1D_POS_MULT_Name,
	StrID_THOR_GENERIC_1D_MIX_Name,
	StrID_THOR_GENERIC_1D_END_Name,
	StrID_THOR_GENERIC_2D_START_Name,
	StrID_THOR_GENERIC_2D_VALUE_1_Name,
	StrID_THOR_GENERIC_2D_VALUE_2_Name,
	StrID_THOR_GENERIC_2D_POS_MULT_Name,
	StrID_THOR_GENERIC_2D_MIX_Name,
	StrID_THOR_GENERIC_2D_END_Name,
	StrID_THOR_GENERIC_3D_START_Name,
	StrID_THOR_GENERIC_3D_VALUE_1_Name,
	StrID_THOR_GENERIC_3D_VALUE_2_Name,
	StrID_THOR_GENERIC_3D_VALUE_3_Name,
	StrID_THOR_GENERIC_3D_POS_MULT_Name,
	StrID_THOR_GENERIC_3D_MIX_Name,
	StrID_THOR_GENERIC_3D_END_Name,
	StrID_THOR_PERLIN_2D_START_Name,
	StrID_THOR_PERLIN_2D_VALUE_1_Name,
	StrID_THOR_PERLIN_2D_VALUE_2_Name,
	StrID_THOR_PERLIN_2D_DIM_Name,
	StrID_THOR_PERLIN_2D_FREQ_Name,
	StrID_THOR_PERLIN_2D_POS_MULT_Name,
	StrID_THOR_PERLIN_2D_MIX_Name,
	StrID_THOR_PERLIN_2D_END_Name,
	StrID_THOR_PERLIN_3D_START_Name,
	StrID_THOR_PERLIN_3D_VALUE_1_Name,
	StrID_THOR_PERLIN_3D_VALUE_2_Name,
	StrID_THOR_PERLIN_3D_VALUE_3_Name,
	StrID_THOR_PERLIN_3D_POS_MULT_Name,
	StrID_THOR_PERLIN_3D_MIX_Name,
	StrID_THOR_PERLIN_3D_END_Name,
	StrID_THOR_PERLIN_4D_START_Name,
	StrID_THOR_PERLIN_4D_VALUE_1_Name,
	StrID_THOR_PERLIN_4D_VALUE_2_Name,
	StrID_THOR_PERLIN_4D_VALUE_3_Name,
	StrID_THOR_PERLIN_4D_VALUE_4_Name,
	StrID_THOR_PERLIN_4D_POS_MULT_Name,
	StrID_THOR_PERLIN_4D_MIX_Name,
	StrID_THOR_PERLIN_4D_END_Name,
	StrID_THOR_SIMPLEX_2D_START_Name,
	StrID_THOR_SIMPLEX_2D_VALUE_1_Name,
	StrID_THOR_SIMPLEX_2D_VALUE_2_Name,
	StrID_THOR_SIMPLEX_2D_POS_MULT_Name,
	StrID_THOR_SIMPLEX_2D_MIX_Name,
	StrID_THOR_SIMPLEX_2D_END_Name,
	StrID_THOR_SIMPLEX_3D_START_Name,
	StrID_THOR_SIMPLEX_3D_VALUE_1_Name,
	StrID_THOR_SIMPLEX_3D_VALUE_2_Name,
	StrID_THOR_SIMPLEX_3D_VALUE_3_Name,
	StrID_THOR_SIMPLEX_3D_POS_MULT_Name,
	StrID_THOR_SIMPLEX_3D_MIX_Name,
	StrID_THOR_SIMPLEX_3D_END_Name,
	StrID_THOR_SIMPLEX_4D_START_Name,
	StrID_THOR_SIMPLEX_4D_VALUE_1_Name,
	StrID_THOR_SIMPLEX_4D_VALUE_2_Name,
	StrID_THOR_SIMPLEX_4D_VALUE_3_Name,
	StrID_THOR_SIMPLEX_4D_VALUE_4_Name,
	StrID_THOR_SIMPLEX_4D_POS_MULT_Name,
	StrID_THOR_SIMPLEX_4D_MIX_Name,
	StrID_THOR_SIMPLEX_4D_END_Name,
	StrID_THOR_VIQ_2D_START_Name,
	StrID_THOR_VIQ_2D_VALUE_1_Name,
	StrID_THOR_VIQ_2D_VALUE_2_Name,
	StrID_THOR_VIQ_2D_U_Name,
	StrID_THOR_VIQ_2D_V_Name,
	StrID_THOR_VIQ_2D_POS_MULT_Name,
	StrID_THOR_VIQ_2D_MIX_Name,
	StrID_THOR_VIQ_2D_END_Name,
	StrID_THOR_VORONOI_2D_START_Name,
	StrID_THOR_VORONOI_2D_VALUE_1_Name,
	StrID_THOR_VORONOI_2D_VALUE_2_Name,
	StrID_THOR_VORONOI_2D_POS_MULT_Name,
	StrID_THOR_VORONOI_2D_MIX_Name,
	StrID_THOR_VORONOI_2D_END_Name,
	StrID_THOR_FRACTBROWN_1D_START_Name,
	StrID_THOR_FRACTBROWN_1D_VALUE_1_Name,
	StrID_THOR_FRACTBROWN_1D_POS_MULT_Name,
	StrID_THOR_FRACTBROWN_1D_MIX_Name,
	StrID_THOR_FRACTBROWN_1D_END_Name,
	StrID_THOR_FRACTBROWN_2D_START_Name,
	StrID_THOR_FRACTBROWN_2D_VALUE_1_Name,
	StrID_THOR_FRACTBROWN_2D_VALUE_2_Name,
	StrID_THOR_FRACTBROWN_2D_POS_MULT_Name,
	StrID_THOR_FRACTBROWN_2D_MIX_Name,
	StrID_THOR_FRACTBROWN_2D_END_Name,
	StrID_THOR_FRACTBROWN_3D_START_Name,
	StrID_THOR_FRACTBROWN_3D_VALUE_1_Name,
	StrID_THOR_FRACTBROWN_3D_VALUE_2_Name,
	StrID_THOR_FRACTBROWN_3D_VALUE_3_Name,
	StrID_THOR_FRACTBROWN_3D_POS_MULT_Name,
	StrID_THOR_FRACTBROWN_3D_MIX_Name,
	StrID_THOR_FRACTBROWN_3D_END_Name,
	StrID_THOR_FRACTBROWN_IQ_START_Name,
	StrID_THOR_FRACTBROWN_IQ_VALUE_1_Name,
	StrID_THOR_FRACTBROWN_IQ_VALUE_2_Name,
	StrID_THOR_FRACTBROWN_IQ_VALUE_3_Name,
	StrID_THOR_FRACTBROWN_IQ_VALUE_4_Name,
	StrID_THOR_FRACTBROWN_IQ_POS_MULT_Name,
	StrID_THOR_FRACTBROWN_IQ_MIX_Name,
	StrID_THOR_FRACTBROWN_IQ_END_Name,
	StrID_THOR_DISPLACE_START_Name,
	StrID_THOR_GENERIC_1D_CB_Name,
	StrID_THOR_GENERIC_2D_CB_Name,
	StrID_THOR_GENERIC_3D_CB_Name,
	StrID_THOR_PERLIN_2D_CB_Name,
	StrID_THOR_PERLIN_3D_CB_Name,
	StrID_THOR_PERLIN_4D_CB_Name,
	StrID_THOR_SIMPLEX_2D_CB_Name,
	StrID_THOR_SIMPLEX_3D_CB_Name,
	StrID_THOR_SIMPLEX_4D_CB_Name,
	StrID_THOR_VIQ_2D_CB_Name,
	StrID_THOR_VORONOI_2D_CB_Name,
	StrID_THOR_FACTBROWN_1D_CB_Name,
	StrID_THOR_FACTBROWN_2D_CB_Name,
	StrID_THOR_FACTBROWN_3D_CB_Name,
	StrID_THOR_FACTBROWN_4D_CB_Name,
	StrID_THOR_DISPLACE_END_Name,
	StrID_Checkbox_Param_Name,	
	StrID_Checkbox_Description,
	StrID_DependString1,
	StrID_DependString2,
	StrID_Err_LoadSuite,
	StrID_Err_FreeSuite,
	StrID_3D_Param_Name,
	StrID_3D_Param_Description,
	StrID_NUMTYPES
} StrIDType;
