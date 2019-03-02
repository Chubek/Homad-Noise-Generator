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

#include "GLator.h"


typedef struct {
	unsigned long	index;
	char			str[256];
} TableString;


TableString		g_strs[StrID_NUMTYPES] = {
	StrID_NONE,						"",
	StrID_Name,						"Thoryvos Noise Machine",
	StrID_Description,				"Copyright(C) 2019.\nBy Chubak Bidpaa\nThoryvos means \"Noise\" In Greek.",
	StrID_Color_Param_Name,			"Color",
StrID_THOR_GENERIC_1D_START_Name,		"Generic 1D",
StrID_THOR_GENERIC_1D_VALUE_1_Name,		"Generic 1D Value",
StrID_THOR_GENERIC_1D_POS_MULT_Name,		"Generic 1D Position",
StrID_THOR_GENERIC_1D_MIX_Name,		"Generic 1D Mix",
StrID_THOR_GENERIC_1D_END_Name,		"Generic 1D End",
StrID_THOR_GENERIC_2D_START_Name,		"Generic 2D",
StrID_THOR_GENERIC_2D_VALUE_1_Name,		"Generic 2D Value 1",
StrID_THOR_GENERIC_2D_VALUE_2_Name,		"Generic 2D Value 2",
StrID_THOR_GENERIC_2D_POS_MULT_Name,		"Generic 2D Position",
StrID_THOR_GENERIC_2D_MIX_Name,		"Generic 2D Mix",
StrID_THOR_GENERIC_2D_END_Name,		"Generic 2D End",
StrID_THOR_GENERIC_3D_START_Name,		"Generic 3D",
StrID_THOR_GENERIC_3D_VALUE_1_Name,		"Generic 3D Value 1",
StrID_THOR_GENERIC_3D_VALUE_2_Name,		"Generic 3D Value 2",
StrID_THOR_GENERIC_3D_VALUE_3_Name,		"Generic 3D Value 3",
StrID_THOR_GENERIC_3D_POS_MULT_Name,		"Generic 3D Position",
StrID_THOR_GENERIC_3D_MIX_Name,		"Generic 3D Mix",
StrID_THOR_GENERIC_3D_END_Name,		"Generic 3D End",
StrID_THOR_PERLIN_2D_START_Name,		"Perlin 2D",
StrID_THOR_PERLIN_2D_VALUE_1_Name,		"Perlin 2D Value 1",
StrID_THOR_PERLIN_2D_VALUE_2_Name,		"Perlin 2D Value 2",
StrID_THOR_PERLIN_2D_DIM_Name,		"Perlin 2D Dimension",
StrID_THOR_PERLIN_2D_FREQ_Name,		"Perlin 2D Frequency",
StrID_THOR_PERLIN_2D_POS_MULT_Name,		"Perlin 2D Position",
StrID_THOR_PERLIN_2D_MIX_Name,		"Perlin 2D Mix",
StrID_THOR_PERLIN_2D_END_Name,		"Perlin 2D End",
StrID_THOR_PERLIN_3D_START_Name,		"Perlin 3D",
StrID_THOR_PERLIN_3D_VALUE_1_Name,		"Perlin 3D Value 1",
StrID_THOR_PERLIN_3D_VALUE_2_Name,		"Perlin 3D Value 2",
StrID_THOR_PERLIN_3D_VALUE_3_Name,		"Perlin 3D Value 3",
StrID_THOR_PERLIN_3D_POS_MULT_Name,		"Perlin 3D Position",
StrID_THOR_PERLIN_3D_MIX_Name,		"Perlin 3D Mix",
StrID_THOR_PERLIN_3D_END_Name,		"Perlin 3D End",
StrID_THOR_PERLIN_4D_START_Name,		"Perlin 4D",
StrID_THOR_PERLIN_4D_VALUE_1_Name,		"Perlin 4D Value 1",
StrID_THOR_PERLIN_4D_VALUE_2_Name,		"Perlin 4D Value 2",
StrID_THOR_PERLIN_4D_VALUE_3_Name,		"Perlin 4D Value 3",
StrID_THOR_PERLIN_4D_VALUE_4_Name,		"Perlin 4D Value 4",
StrID_THOR_PERLIN_4D_POS_MULT_Name,		"Perlin 4D Position",
StrID_THOR_PERLIN_4D_MIX_Name,		"Perlin 4D Mix",
StrID_THOR_PERLIN_4D_END_Name,		"Perlin 4D End",
StrID_THOR_SIMPLEX_2D_START_Name,		"Simplex 2D",
StrID_THOR_SIMPLEX_2D_VALUE_1_Name,		"Simplex 2D Value 1",
StrID_THOR_SIMPLEX_2D_VALUE_2_Name,		"Simplex 2D Value 2",
StrID_THOR_SIMPLEX_2D_POS_MULT_Name,		"Simplex 2D Position",
StrID_THOR_SIMPLEX_2D_MIX_Name,		"Simplex 2D Mix",
StrID_THOR_SIMPLEX_2D_END_Name,		"Simplex 2D End",
StrID_THOR_SIMPLEX_3D_START_Name,		"Simplex 3D",
StrID_THOR_SIMPLEX_3D_VALUE_1_Name,		"Simplex 3D Value 1",
StrID_THOR_SIMPLEX_3D_VALUE_2_Name,		"Simplex 3D Value 2",
StrID_THOR_SIMPLEX_3D_VALUE_3_Name,		"Simplex 3D Value 3",
StrID_THOR_SIMPLEX_3D_POS_MULT_Name,		"Simplex 3D Position",
StrID_THOR_SIMPLEX_3D_MIX_Name,		"Simplex 3D Mix",
StrID_THOR_SIMPLEX_3D_END_Name,		"Simplex 3D End",
StrID_THOR_SIMPLEX_4D_START_Name,		"Simplex 4D",
StrID_THOR_SIMPLEX_4D_VALUE_1_Name,		"Simplex 4D Value 1",
StrID_THOR_SIMPLEX_4D_VALUE_2_Name,		"Simplex 4D Value 2",
StrID_THOR_SIMPLEX_4D_VALUE_3_Name,		"Simplex 4D Value 3",
StrID_THOR_SIMPLEX_4D_VALUE_4_Name,		"Simplex 4D Value 4",
StrID_THOR_SIMPLEX_4D_POS_MULT_Name,		"Simplex 4D Position",
StrID_THOR_SIMPLEX_4D_MIX_Name,		"Simplex 4D Mix",
StrID_THOR_SIMPLEX_4D_END_Name,		"Simplex 4D End",
StrID_THOR_VIQ_2D_START_Name,		"Voronoi Inigo Quilez 2D",
StrID_THOR_VIQ_2D_VALUE_1_Name,		"Voronoi IQ 2D Value 1",
StrID_THOR_VIQ_2D_VALUE_2_Name,		"Voronoi IQ 2D Value 2",
StrID_THOR_VIQ_2D_U_Name,	"Voronoi IQ 2D U",
StrID_THOR_VIQ_2D_V_Name,	"Voronoi IQ 2D V",
StrID_THOR_VIQ_2D_POS_MULT_Name,		"Voronoi IQ 2D Position",
StrID_THOR_VIQ_2D_MIX_Name,		"Voronoi IQ 2D Mix",
StrID_THOR_VIQ_2D_END_Name,		"Voronoi IQ 2D End",
StrID_THOR_VORONOI_2D_START_Name,		"Voronoi 2D",
StrID_THOR_VORONOI_2D_VALUE_1_Name,		"Voronoi 2D Value 1",
StrID_THOR_VORONOI_2D_VALUE_2_Name,		"Voronoi 2D Value 2",
StrID_THOR_VORONOI_2D_POS_MULT_Name,		"Voronoi 2D Position",
StrID_THOR_VORONOI_2D_MIX_Name,		"Voronoi 2D Mix",
StrID_THOR_VORONOI_2D_END_Name,		"Voronoi 2D End",
StrID_THOR_FRACTBROWN_1D_START_Name,		"Fractbrown 1D",
StrID_THOR_FRACTBROWN_1D_VALUE_1_Name,		"Fractbrown 1D Value 1",
StrID_THOR_FRACTBROWN_1D_POS_MULT_Name,		"Fractbrown 1D Position",
StrID_THOR_FRACTBROWN_1D_MIX_Name,		"Fractbrown 1D Mix",
StrID_THOR_FRACTBROWN_1D_END_Name,		"Fractbrown 1D End",
StrID_THOR_FRACTBROWN_2D_START_Name,		"Fractbrown 2D",
StrID_THOR_FRACTBROWN_2D_VALUE_1_Name,		"Fractbrown 2D Value 1",
StrID_THOR_FRACTBROWN_2D_VALUE_2_Name,		"Fractbrown 2D Value 2",
StrID_THOR_FRACTBROWN_2D_POS_MULT_Name,		"Fractbrown 2D Position",
StrID_THOR_FRACTBROWN_2D_MIX_Name,		"Fractbrown 2D Mix",
StrID_THOR_FRACTBROWN_2D_END_Name,		"Fractbrown 2D End",
StrID_THOR_FRACTBROWN_3D_START_Name,		"Fractbrown 3D",
StrID_THOR_FRACTBROWN_3D_VALUE_1_Name,		"Fractbrown 3D Value 1",
StrID_THOR_FRACTBROWN_3D_VALUE_2_Name,		"Fractbrown 3D Value 2",
StrID_THOR_FRACTBROWN_3D_VALUE_3_Name,		"Fractbrown 3D Value 3",
StrID_THOR_FRACTBROWN_3D_POS_MULT_Name,		"Fractbrown 3D Position",
StrID_THOR_FRACTBROWN_3D_MIX_Name,		"Fractbrown 3D Mix",
StrID_THOR_FRACTBROWN_3D_END_Name,		"Fractbrown 3D End",
StrID_THOR_FRACTBROWN_IQ_START_Name,		"Fractbrown Inigo Quilez",
StrID_THOR_FRACTBROWN_IQ_VALUE_1_Name,		"Fractbrown IQ Value 1",
StrID_THOR_FRACTBROWN_IQ_VALUE_2_Name,		"Fractbrown IQ Value 2",
StrID_THOR_FRACTBROWN_IQ_VALUE_3_Name,		"Fractbrown IQ Value 3",
StrID_THOR_FRACTBROWN_IQ_VALUE_4_Name,		"Fractbrown IQ Value 4",
StrID_THOR_FRACTBROWN_IQ_POS_MULT_Name,		"Fractbrown IQ Position",
StrID_THOR_FRACTBROWN_IQ_MIX_Name,		"Fractbrown IQ Mix",
StrID_THOR_FRACTBROWN_IQ_END_Name,		"Fractbrown IQ End",
StrID_THOR_DISPLACE_START_Name,		"Displace",
StrID_THOR_GENERIC_1D_CB_Name,		"Generic 1D Toggle",
StrID_THOR_GENERIC_2D_CB_Name,		"Generic 2D Toggle",
StrID_THOR_GENERIC_3D_CB_Name,		"Generic 3D Toggle",
StrID_THOR_PERLIN_2D_CB_Name,		"Perlin 2D Toggle",
StrID_THOR_PERLIN_3D_CB_Name,		"Perlin 3D Toggle",
StrID_THOR_PERLIN_4D_CB_Name,		"Perlin 4D Toggle",
StrID_THOR_SIMPLEX_2D_CB_Name,		"Simplex 2D Toggle",
StrID_THOR_SIMPLEX_3D_CB_Name,		"Simplex 3D Toggle",
StrID_THOR_SIMPLEX_4D_CB_Name,		"Simplex 4D Toggle",
StrID_THOR_VIQ_2D_CB_Name,		"Voronoi IQ 2D Toggle",
StrID_THOR_VORONOI_2D_CB_Name,		"Voronoi 2D Toggle",
StrID_THOR_FACTBROWN_1D_CB_Name,		"Fractbrown 1D Toggle",
StrID_THOR_FACTBROWN_2D_CB_Name,		"Fractbrown 2D Toggle",
StrID_THOR_FACTBROWN_3D_CB_Name,		"Fractbrown 3D Toggle",
StrID_THOR_FACTBROWN_4D_CB_Name,		"Fractbrown 4D Toggle",
StrID_THOR_DISPLACE_END_Name,		"Displace End",
	StrID_Checkbox_Param_Name,		"Use Downsample Factors",
	StrID_Checkbox_Description,		"Correct at all resolutions",
	StrID_DependString1,			"All Dependencies requested.",
	StrID_DependString2,			"Missing Dependencies requested.",
	StrID_Err_LoadSuite,			"Error loading suite.",
	StrID_Err_FreeSuite,			"Error releasing suite.",
	StrID_3D_Param_Name,			"Use lights and cameras",

};


char	*GetStringPtr(int strNum)
{
	return g_strs[strNum].str;
}

	