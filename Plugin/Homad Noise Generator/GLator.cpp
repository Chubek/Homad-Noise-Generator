/*******************************************************************/
/*                                                                 */
/*                      ADOBE CONFIDENTIAL                         */
/*                   _ _ _ _ _ _ _ _ _ _ _ _ _                     */
/*                                                                 */
/* Copyright 2007-2015 Adobe Systems Incorporated                  */
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

/*	GLator.cpp	

	This is a sample OpenGL plugin. The framework is done for you.
	Use it to create more funky effects.
	
	Revision History

	Version		Change													Engineer	Date
	=======		======													========	======
	1.0			Win and Mac versions use the same base files.			anindyar	7/4/2007
	1.1			Add OpenGL context switching to play nicely with
				AE's own OpenGL usage (thanks Brendan Bolles!)			zal			8/13/2012
	2.0			Completely re-written for OGL 3.3 and threads			aparente	9/30/2015
	2.1			Added new entry point									zal			9/15/2017

*/

#include "GLator.h"

#include "GL_base.h"
#include "Smart_Utils.h"
#include "AEFX_SuiteHelper.h"

#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include "vmath.hpp"
#include <assert.h>

using namespace AESDK_OpenGL;
using namespace gl33core;

#include "glbinding/gl33ext/gl.h"
#include <glbinding/gl/extension.h>

/* AESDK_OpenGL effect specific variables */

namespace {
	THREAD_LOCAL int t_thread = -1;

	std::atomic_int S_cnt;
	std::map<int, std::shared_ptr<AESDK_OpenGL::AESDK_OpenGL_EffectRenderData> > S_render_contexts;
	std::recursive_mutex S_mutex;

	AESDK_OpenGL::AESDK_OpenGL_EffectCommonDataPtr S_GLator_EffectCommonData; //global context
	std::string S_ResourcePath;

	// - OpenGL resources are restricted per thread, mimicking the OGL driver
	// - The filter will eliminate all TLS (Thread Local Storage) at PF_Cmd_GLOBAL_SETDOWN
	AESDK_OpenGL::AESDK_OpenGL_EffectRenderDataPtr GetCurrentRenderContext()
	{
		S_mutex.lock();
		AESDK_OpenGL::AESDK_OpenGL_EffectRenderDataPtr result;

		if (t_thread == -1) {
			t_thread = S_cnt++;

			result.reset(new AESDK_OpenGL::AESDK_OpenGL_EffectRenderData());
			S_render_contexts[t_thread] = result;
		}
		else {
			result = S_render_contexts[t_thread];
		}
		S_mutex.unlock();
		return result;
	}

#ifdef AE_OS_WIN
	std::string get_string_from_wcs(const wchar_t* pcs)
	{
		int res = WideCharToMultiByte(CP_ACP, 0, pcs, -1, NULL, 0, NULL, NULL);

		std::auto_ptr<char> shared_pbuf(new char[res]);

		char *pbuf = shared_pbuf.get();

		res = WideCharToMultiByte(CP_ACP, 0, pcs, -1, pbuf, res, NULL, NULL);

		return std::string(pbuf);
	}
#endif

	void RenderQuad(GLuint vbo)
	{
		glEnableVertexAttribArray(PositionSlot);
		glEnableVertexAttribArray(UVSlot);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glVertexAttribPointer(PositionSlot, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);
		glVertexAttribPointer(UVSlot, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glDisableVertexAttribArray(PositionSlot);
		glDisableVertexAttribArray(UVSlot);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	std::string GetResourcesPath(PF_InData		*in_data)
	{
		//initialize and compile the shader objects
		A_UTF16Char pluginFolderPath[AEFX_MAX_PATH];
		PF_GET_PLATFORM_DATA(PF_PlatData_EXE_FILE_PATH_W, &pluginFolderPath);

#ifdef AE_OS_WIN
		std::string resourcePath = get_string_from_wcs((wchar_t*)pluginFolderPath);
		std::string::size_type pos;
		//delete the plugin name
		pos = resourcePath.rfind("\\", resourcePath.length());
		resourcePath = resourcePath.substr(0, pos) + "\\";
#endif
#ifdef AE_OS_MAC
		NSUInteger length = 0;
		A_UTF16Char* tmp = pluginFolderPath;
		while (*tmp++ != 0) {
			++length;
		}
		NSString* newStr = [[NSString alloc] initWithCharacters:pluginFolderPath length : length];
		std::string resourcePath([newStr UTF8String]);
		resourcePath += "/Contents/Resources/";
#endif
		return resourcePath;
	}

	struct CopyPixelFloat_t {
		PF_PixelFloat	*floatBufferP;
		PF_EffectWorld	*input_worldP;
	};

	PF_Err
	CopyPixelFloatIn(
		void			*refcon,
		A_long			x,
		A_long			y,
		PF_PixelFloat	*inP,
		PF_PixelFloat	*)
	{
		CopyPixelFloat_t	*thiS = reinterpret_cast<CopyPixelFloat_t*>(refcon);
		PF_PixelFloat		*outP = thiS->floatBufferP + y * thiS->input_worldP->width + x;

		outP->red = inP->red;
		outP->green = inP->green;
		outP->blue = inP->blue;
		outP->alpha = inP->alpha;

		return PF_Err_NONE;
	}

	PF_Err
	CopyPixelFloatOut(
		void			*refcon,
		A_long			x,
		A_long			y,
		PF_PixelFloat	*,
		PF_PixelFloat	*outP)
	{
		CopyPixelFloat_t		*thiS = reinterpret_cast<CopyPixelFloat_t*>(refcon);
		const PF_PixelFloat		*inP = thiS->floatBufferP + y * thiS->input_worldP->width + x;

		outP->red = inP->red;
		outP->green = inP->green;
		outP->blue = inP->blue;
		outP->alpha = inP->alpha;

		return PF_Err_NONE;
	}


	gl::GLuint UploadTexture(AEGP_SuiteHandler& suites,					// >>
							 PF_PixelFormat			format,				// >>
							 PF_EffectWorld			*input_worldP,		// >>
							 PF_EffectWorld			*output_worldP,		// >>
							 PF_InData				*in_data,			// >>
							 size_t& pixSizeOut,						// <<
							 gl::GLenum& glFmtOut,						// <<
							 float& multiplier16bitOut)					// <<
	{
		// - upload to texture memory
		// - we will convert on-the-fly from ARGB to RGBA, and also to pre-multiplied alpha,
		// using a fragment shader
#ifdef _DEBUG
		GLint nUnpackAlignment;
		::glGetIntegerv(GL_UNPACK_ALIGNMENT, &nUnpackAlignment);
		assert(nUnpackAlignment == 4);
#endif

		gl::GLuint inputFrameTexture;
		glGenTextures(1, &inputFrameTexture);
		glBindTexture(GL_TEXTURE_2D, inputFrameTexture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_2D, 0, (GLint)GL_RGBA32F, input_worldP->width, input_worldP->height, 0, GL_RGBA, GL_FLOAT, nullptr);

		multiplier16bitOut = 1.0f;
		switch (format)
		{
		case PF_PixelFormat_ARGB128:
		{
			glFmtOut = GL_FLOAT;
			pixSizeOut = sizeof(PF_PixelFloat);

			std::auto_ptr<PF_PixelFloat> bufferFloat(new PF_PixelFloat[input_worldP->width * input_worldP->height]);
			CopyPixelFloat_t refcon = { bufferFloat.get(), input_worldP };

			CHECK(suites.IterateFloatSuite1()->iterate(in_data,
				0,
				input_worldP->height,
				input_worldP,
				nullptr,
				reinterpret_cast<void*>(&refcon),
				CopyPixelFloatIn,
				output_worldP));

			glPixelStorei(GL_UNPACK_ROW_LENGTH, input_worldP->width);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, input_worldP->width, input_worldP->height, GL_RGBA, GL_FLOAT, bufferFloat.get());
			break;
		}

		case PF_PixelFormat_ARGB64:
		{
			glFmtOut = GL_UNSIGNED_SHORT;
			pixSizeOut = sizeof(PF_Pixel16);
			multiplier16bitOut = 65535.0f / 32768.0f;

			glPixelStorei(GL_UNPACK_ROW_LENGTH, input_worldP->rowbytes / sizeof(PF_Pixel16));
			PF_Pixel16 *pixelDataStart = NULL;
			PF_GET_PIXEL_DATA16(input_worldP, NULL, &pixelDataStart);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, input_worldP->width, input_worldP->height, GL_RGBA, GL_UNSIGNED_SHORT, pixelDataStart);
			break;
		}

		case PF_PixelFormat_ARGB32:
		{
			glFmtOut = GL_UNSIGNED_BYTE;
			pixSizeOut = sizeof(PF_Pixel8);

			glPixelStorei(GL_UNPACK_ROW_LENGTH, input_worldP->rowbytes / sizeof(PF_Pixel8));
			PF_Pixel8 *pixelDataStart = NULL;
			PF_GET_PIXEL_DATA8(input_worldP, NULL, &pixelDataStart);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, input_worldP->width, input_worldP->height, GL_RGBA, GL_UNSIGNED_BYTE, pixelDataStart);
			break;
		}

		default:
			CHECK(PF_Err_BAD_CALLBACK_PARAM);
			break;
		}

		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

		//unbind all textures
		glBindTexture(GL_TEXTURE_2D, 0);

		return inputFrameTexture;
	}

	void ReportIfErrorFramebuffer(PF_InData *in_data, PF_OutData *out_data)
	{
		// Check for errors...
		std::string error_msg;
		if ((error_msg = CheckFramebufferStatus()) != std::string("OK"))
		{
			out_data->out_flags |= PF_OutFlag_DISPLAY_ERROR_MESSAGE;
			PF_SPRINTF(out_data->return_msg, error_msg.c_str());
			CHECK(PF_Err_OUT_OF_MEMORY);
		}
	}


	void SwizzleGL(const AESDK_OpenGL::AESDK_OpenGL_EffectRenderDataPtr& renderContext,
				   A_long widthL, A_long heightL,
				   gl::GLuint		inputFrameTexture,
				   float			multiplier16bit)
	{
		glBindTexture(GL_TEXTURE_2D, inputFrameTexture);

		glUseProgram(renderContext->mProgramObj2Su);

		// view matrix, mimic windows coordinates
		vmath::Matrix4 ModelviewProjection = vmath::Matrix4::translation(vmath::Vector3(-1.0f, -1.0f, 0.0f)) *
			vmath::Matrix4::scale(vmath::Vector3(2.0 / float(widthL), 2.0 / float(heightL), 1.0f));

		GLint location = glGetUniformLocation(renderContext->mProgramObj2Su, "ModelviewProjection");
		glUniformMatrix4fv(location, 1, GL_FALSE, (GLfloat*)&ModelviewProjection);
		location = glGetUniformLocation(renderContext->mProgramObj2Su, "multiplier16bit");
		glUniform1f(location, multiplier16bit);

		AESDK_OpenGL_BindTextureToTarget(renderContext->mProgramObj2Su, inputFrameTexture, std::string("videoTexture"));

		// render
		glBindVertexArray(renderContext->vao);
		RenderQuad(renderContext->quad);
		glBindVertexArray(0);

		glUseProgram(0);

		glFlush();
	}

	void RenderGL(const AESDK_OpenGL::AESDK_OpenGL_EffectRenderDataPtr& renderContext,
				  A_long widthL, A_long heightL,
				  gl::GLuint		inputFrameTexture,
				  PF_FpLong			sliderVal,
				  float				multiplier16bit)
	{
		// - make sure we blend correctly inside the framebuffer
		// - even though we just cleared it, another effect may want to first
		// draw some kind of background to blend with
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquation(GL_FUNC_ADD);

		// view matrix, mimic windows coordinates
		vmath::Matrix4 ModelviewProjection = vmath::Matrix4::translation(vmath::Vector3(-1.0f, -1.0f, 0.0f)) *
			vmath::Matrix4::scale(vmath::Vector3(2.0 / float(widthL), 2.0 / float(heightL), 1.0f));

		glBindTexture(GL_TEXTURE_2D, inputFrameTexture);

		glUseProgram(renderContext->mProgramObjSu);

		// program uniforms
		GLint location = glGetUniformLocation(renderContext->mProgramObjSu, "ModelviewProjection");
		glUniformMatrix4fv(location, 1, GL_FALSE, (GLfloat*)&ModelviewProjection);
		location = glGetUniformLocation(renderContext->mProgramObjSu, "sliderVal");
		glUniform1f(location, sliderVal);
		location = glGetUniformLocation(renderContext->mProgramObjSu, "multiplier16bit");
		glUniform1f(location, multiplier16bit);

		// Identify the texture to use and bind it to texture unit 0
		AESDK_OpenGL_BindTextureToTarget(renderContext->mProgramObjSu, inputFrameTexture, std::string("videoTexture"));

		// render
		glBindVertexArray(renderContext->vao);
		RenderQuad(renderContext->quad);
		glBindVertexArray(0);

		glUseProgram(0);
		glDisable(GL_BLEND);
	}

	void DownloadTexture(const AESDK_OpenGL::AESDK_OpenGL_EffectRenderDataPtr& renderContext,
						 AEGP_SuiteHandler&		suites,				// >>
						 PF_EffectWorld			*input_worldP,		// >>
						 PF_EffectWorld			*output_worldP,		// >>
						 PF_InData				*in_data,			// >>
						 PF_PixelFormat			format,				// >>
						 size_t					pixSize,			// >>
						 gl::GLenum				glFmt				// >>
						 )
	{
		//download from texture memory onto the same surface
		PF_Handle bufferH = NULL;
		bufferH = suites.HandleSuite1()->host_new_handle(((renderContext->mRenderBufferWidthSu * renderContext->mRenderBufferHeightSu)* pixSize));
		if (!bufferH) {
			CHECK(PF_Err_OUT_OF_MEMORY);
		}
		void *bufferP = suites.HandleSuite1()->host_lock_handle(bufferH);

		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glReadPixels(0, 0, renderContext->mRenderBufferWidthSu, renderContext->mRenderBufferHeightSu, GL_RGBA, glFmt, bufferP);

		switch (format)
		{
		case PF_PixelFormat_ARGB128:
		{
			PF_PixelFloat* bufferFloatP = reinterpret_cast<PF_PixelFloat*>(bufferP);
			CopyPixelFloat_t refcon = { bufferFloatP, input_worldP };

			CHECK(suites.IterateFloatSuite1()->iterate(in_data,
				0,
				input_worldP->height,
				input_worldP,
				nullptr,
				reinterpret_cast<void*>(&refcon),
				CopyPixelFloatOut,
				output_worldP));
			break;
		}

		case PF_PixelFormat_ARGB64:
		{
			PF_Pixel16* buffer16P = reinterpret_cast<PF_Pixel16*>(bufferP);

			//copy to output_worldP
			for (int y = 0; y < output_worldP->height; ++y)
			{
				PF_Pixel16 *pixelDataStart = NULL;
				PF_GET_PIXEL_DATA16(output_worldP, NULL, &pixelDataStart);
				::memcpy(pixelDataStart + (y * output_worldP->rowbytes / sizeof(PF_Pixel16)),
					buffer16P + (y * renderContext->mRenderBufferWidthSu),
					output_worldP->width * sizeof(PF_Pixel16));
			}
			break;
		}

		case PF_PixelFormat_ARGB32:
		{
			PF_Pixel8 *buffer8P = reinterpret_cast<PF_Pixel8*>(bufferP);

			//copy to output_worldP
			for (int y = 0; y < output_worldP->height; ++y)
			{
				PF_Pixel8 *pixelDataStart = NULL;
				PF_GET_PIXEL_DATA8(output_worldP, NULL, &pixelDataStart);
				::memcpy(pixelDataStart + (y * output_worldP->rowbytes / sizeof(PF_Pixel8)),
					buffer8P + (y * renderContext->mRenderBufferWidthSu),
					output_worldP->width * sizeof(PF_Pixel8));
			}
			break;
		}

		default:
			CHECK(PF_Err_BAD_CALLBACK_PARAM);
			break;
		}

		//clean the data after being copied
		suites.HandleSuite1()->host_unlock_handle(bufferH);
		suites.HandleSuite1()->host_dispose_handle(bufferH);
	}
} // anonymous namespace

static PF_Err 
About (	
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	
	suites.ANSICallbacksSuite1()->sprintf(	out_data->return_msg,
											"%s v%d.%d\r%s",
											STR(StrID_Name), 
											MAJOR_VERSION, 
											MINOR_VERSION, 
											STR(StrID_Description));
	return PF_Err_NONE;
}

static PF_Err
GlobalSetup (	
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	out_data->my_version = PF_VERSION(	MAJOR_VERSION, 
										MINOR_VERSION,
										BUG_VERSION, 
										STAGE_VERSION, 
										BUILD_VERSION);

	out_data->out_flags = 	PF_OutFlag_DEEP_COLOR_AWARE;
	
	out_data->out_flags2 =	PF_OutFlag2_FLOAT_COLOR_AWARE	|
							PF_OutFlag2_SUPPORTS_SMART_RENDER;
	
	PF_Err err = PF_Err_NONE;
	try
	{
		// always restore back AE's own OGL context
		SaveRestoreOGLContext oSavedContext;
		AEGP_SuiteHandler suites(in_data->pica_basicP);

		//Now comes the OpenGL part - OS specific loading to start with
		S_GLator_EffectCommonData.reset(new AESDK_OpenGL::AESDK_OpenGL_EffectCommonData());
		AESDK_OpenGL_Startup(*S_GLator_EffectCommonData.get());
		
		S_ResourcePath = GetResourcesPath(in_data);
	}
	catch(PF_Err& thrown_err)
	{
		err = thrown_err;
	}
	catch (...)
	{
		err = PF_Err_OUT_OF_MEMORY;
	}

	return err;
}

static PF_Err 
ParamsSetup (	
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	PF_Err		err		= PF_Err_NONE;
	PF_ParamDef	def;	

	AEFX_CLR_STRUCT(def);



	PF_ADD_TOPIC(STR(StrID_THOR_GENERIC_1D_START_Name),
		THOR_GENERIC_1D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_1D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_1D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_1D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_1D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_1D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_1D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_GENERIC_1D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC(STR(StrID_THOR_GENERIC_2D_START_Name),
		THOR_GENERIC_2D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_2D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_2D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_2D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_2D_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_2D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_2D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_2D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_2D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_GENERIC_2D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC(STR(StrID_THOR_GENERIC_3D_START_Name),
		THOR_GENERIC_3D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_3D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_3D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_3D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_3D_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end

			//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_3D_VALUE_3_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_3D_VALUE_3_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_3D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_3D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_GENERIC_3D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_GENERIC_3D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_GENERIC_3D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC(STR(StrID_THOR_PERLIN_2D_START_Name),
		THOR_PERLIN_2D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_2D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_2D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_2D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_2D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end

			//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_2D_DIM_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_2D_DIM_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
				//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_2D_FREQ_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_2D_FREQ_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_2D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_2D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_2D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_2D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_PERLIN_2D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);

	PF_ADD_TOPIC(STR(StrID_THOR_PERLIN_3D_START_Name),
		THOR_PERLIN_3D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_3D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_3D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_3D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_3D_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end


	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_3D_VALUE_3_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_3D_VALUE_3_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_3D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_3D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_3D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_3D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_PERLIN_3D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC(STR(StrID_THOR_PERLIN_4D_START_Name),
		THOR_PERLIN_4D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_4D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_4D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_4D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_4D_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end


	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_4D_VALUE_3_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_4D_VALUE_3_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end


	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_4D_VALUE_4_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_4D_VALUE_4_DISK_ID);
	AEFX_CLR_STRUCT(def);
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_4D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_4D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_PERLIN_4D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_PERLIN_4D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_PERLIN_4D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);

	PF_ADD_TOPIC(STR(StrID_THOR_SIMPLEX_2D_START_Name),
		THOR_SIMPLEX_2D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_2D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_2D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_2D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_2D_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end



	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_2D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_2D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_2D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_2D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_SIMPLEX_2D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC(STR(StrID_THOR_SIMPLEX_3D_START_Name),
		THOR_SIMPLEX_3D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_3D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_3D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_3D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_3D_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_3D_VALUE_3_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_3D_VALUE_3_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start


	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_3D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_3D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_3D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_3D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_SIMPLEX_3D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC(STR(StrID_THOR_SIMPLEX_4D_START_Name),
		THOR_SIMPLEX_4D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_4D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_4D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_4D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_4D_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_4D_VALUE_3_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_4D_VALUE_3_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_4D_VALUE_4_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_4D_VALUE_4_DISK_ID);
	AEFX_CLR_STRUCT(def);


	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_4D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_4D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_SIMPLEX_4D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_SIMPLEX_4D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_SIMPLEX_4D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC(STR(StrID_THOR_VIQ_2D_START_Name),
		THOR_VIQ_2D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_VIQ_2D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_VIQ_2D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_VIQ_2D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_VIQ_2D_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_VIQ_2D_U_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_VIQ_2D_U_MULT_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_VIQ_2D_V_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_VIQ_2D_V_MULT_DISK_ID);
	AEFX_CLR_STRUCT(def);


	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_VIQ_2D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_VIQ_2D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_VIQ_2D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_VIQ_2D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_VIQ_2D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);

	PF_ADD_TOPIC(STR(StrID_THOR_VORONOI_2D_START_Name),
		THOR_VORONOI_2D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_VORONOI_2D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_VORONOI_2D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
		//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_VORONOI_2D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_VORONOI_2D_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);




	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_VORONOI_2D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_VORONOI_2D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_VORONOI_2D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_VORONOI_2D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_VORONOI_2D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);



	PF_ADD_TOPIC(STR(StrID_THOR_FRACTBROWN_1D_START_Name),
		THOR_FRACTBROWN_1D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_1D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_1D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_1D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_1D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_1D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_1D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_FRACTBROWN_1D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);

	PF_ADD_TOPIC(STR(StrID_THOR_FRACTBROWN_2D_START_Name),
		THOR_FRACTBROWN_2D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_2D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_2D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_2D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_2D_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_2D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_2D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_2D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_2D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_FRACTBROWN_2D_END_DISK_ID);
	AEFX_CLR_STRUCT(def)

	PF_ADD_TOPIC(STR(StrID_THOR_FRACTBROWN_3D_START_Name),
		THOR_FRACTBROWN_3D_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_3D_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_3D_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_3D_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_3D_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_3D_VALUE_3_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_3D_VALUE_3_DISK_ID);
	AEFX_CLR_STRUCT(def);

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_3D_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_3D_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);



	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_3D_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_3D_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_FRACTBROWN_3D_END_DISK_ID);
	AEFX_CLR_STRUCT(def);

	PF_ADD_TOPIC(STR(StrID_THOR_FRACTBROWN_IQ_START_Name),
		THOR_FRACTBROWN_IQ_START_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_IQ_VALUE_1_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_IQ_VALUE_1_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	//slider_start

	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_IQ_VALUE_2_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_IQ_VALUE_2_DISK_ID);
	AEFX_CLR_STRUCT(def);

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_IQ_VALUE_3_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_IQ_VALUE_3_DISK_ID);
	AEFX_CLR_STRUCT(def);

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_IQ_VALUE_4_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_IQ_VALUE_4_DISK_ID);
	AEFX_CLR_STRUCT(def);

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_IQ_POS_MULT_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_IQ_POS_MULT_DISK_ID);

	AEFX_CLR_STRUCT(def);



	//slider_end
	//slider_start

	PF_ADD_FLOAT_SLIDERX(STR(StrID_THOR_FRACTBROWN_IQ_MIX_Name),
		THOR_SLIDER_MIN_VAL,
		THOR_SLIDER_MAX_VAL,
		THOR_SLIDER_MIN,
		THOR_SLIDER_MAX,
		THOR_SLIDER_DFLT,
		PF_Precision_TEN_THOUSANDTHS,
		0, 0,
		THOR_FRACTBROWN_IQ_MIX_DISK_ID);
	AEFX_CLR_STRUCT(def);

	//slider_end
	PF_END_TOPIC(THOR_FRACTBROWN_IQ_END_DISK_ID);
	AEFX_CLR_STRUCT(def);


	PF_ADD_TOPIC(STR(StrID_THOR_DISPLACE_START_Name),
		THOR_DISPLACE_START_DISK_ID);
	AEFX_CLR_STRUCT(def);



	PF_ADD_CHECKBOXX(STR(StrID_THOR_GENERIC_1D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_GENERIC_1D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_GENERIC_2D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_GENERIC_2D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_GENERIC_3D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_GENERIC_3D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_PERLIN_2D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_PERLIN_2D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_PERLIN_3D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_PERLIN_3D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_PERLIN_4D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_PERLIN_4D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_SIMPLEX_2D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_SIMPLEX_2D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_SIMPLEX_3D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_SIMPLEX_3D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_SIMPLEX_4D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_SIMPLEX_4D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_VIQ_2D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_VIQ_2D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_VORONOI_2D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_VORONOI_2D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_FACTBROWN_1D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_FACTBROWN_1D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_FACTBROWN_2D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_FACTBROWN_2D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_FACTBROWN_3D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_FACTBROWN_3D_CB_DISK_ID);
	PF_ADD_CHECKBOXX(STR(StrID_THOR_FACTBROWN_4D_CB_Name), THOR_CHECKBOX_DFLT, 0, THOR_FACTBROWN_4D_CB_DISK_ID);

	PF_END_TOPIC(THOR_DISPLACE_END_DISK_ID);




	out_data->num_params = THOR_NUM_PARAMS;

	return err;
}


static PF_Err 
GlobalSetdown (
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output )
{
	PF_Err			err			=	PF_Err_NONE;

	try
	{
		// always restore back AE's own OGL context
		SaveRestoreOGLContext oSavedContext;

		S_mutex.lock();
		S_render_contexts.clear();
		S_mutex.unlock();

		//OS specific unloading
		AESDK_OpenGL_Shutdown(*S_GLator_EffectCommonData.get());
		S_GLator_EffectCommonData.reset();
		S_ResourcePath.clear();

		if (in_data->sequence_data) {
			PF_DISPOSE_HANDLE(in_data->sequence_data);
			out_data->sequence_data = NULL;
		}
	}
	catch(PF_Err& thrown_err)
	{
		err = thrown_err;
	}
	catch (...)
	{
		err = PF_Err_OUT_OF_MEMORY;
	}

	return err;
}

static PF_Err
PreRender(
	PF_InData				*in_data,
	PF_OutData				*out_data,
	PF_PreRenderExtra		*extra)
{
	PF_Err	err = PF_Err_NONE,
			err2 = PF_Err_NONE;

	PF_ParamDef		THOR_GENERIC_1D_START_Param;
	PF_ParamDef		THOR_GENERIC_1D_VALUE_1_Param;
	PF_ParamDef		THOR_GENERIC_1D_POS_MULT_Param;
	PF_ParamDef		THOR_GENERIC_1D_MIX_Param;
	PF_ParamDef		THOR_GENERIC_1D_END_Param;
	PF_ParamDef		THOR_GENERIC_2D_START_Param;
	PF_ParamDef		THOR_GENERIC_2D_VALUE_1_Param;
	PF_ParamDef		THOR_GENERIC_2D_VALUE_2_Param;
	PF_ParamDef		THOR_GENERIC_2D_POS_MULT_Param;
	PF_ParamDef		THOR_GENERIC_2D_MIX_Param;
	PF_ParamDef		THOR_GENERIC_2D_END_Param;
	PF_ParamDef		THOR_GENERIC_3D_START_Param;
	PF_ParamDef		THOR_GENERIC_3D_VALUE_1_Param;
	PF_ParamDef		THOR_GENERIC_3D_VALUE_2_Param;
	PF_ParamDef		THOR_GENERIC_3D_VALUE_3_Param;
	PF_ParamDef		THOR_GENERIC_3D_POS_MULT_Param;
	PF_ParamDef		THOR_GENERIC_3D_MIX_Param;
	PF_ParamDef		THOR_GENERIC_3D_END_Param;
	PF_ParamDef		THOR_PERLIN_2D_START_Param;
	PF_ParamDef		THOR_PERLIN_2D_VALUE_1_Param;
	PF_ParamDef		THOR_PERLIN_2D_VALUE_2_Param;
	PF_ParamDef		THOR_PERLIN_2D_DIM_Param;
	PF_ParamDef		THOR_PERLIN_2D_FREQ_Param;
	PF_ParamDef		THOR_PERLIN_2D_POS_MULT_Param;
	PF_ParamDef		THOR_PERLIN_2D_MIX_Param;
	PF_ParamDef		THOR_PERLIN_2D_END_Param;
	PF_ParamDef		THOR_PERLIN_3D_START_Param;
	PF_ParamDef		THOR_PERLIN_3D_VALUE_1_Param;
	PF_ParamDef		THOR_PERLIN_3D_VALUE_2_Param;
	PF_ParamDef		THOR_PERLIN_3D_VALUE_3_Param;
	PF_ParamDef		THOR_PERLIN_3D_POS_MULT_Param;
	PF_ParamDef		THOR_PERLIN_3D_MIX_Param;
	PF_ParamDef		THOR_PERLIN_3D_END_Param;
	PF_ParamDef		THOR_PERLIN_4D_START_Param;
	PF_ParamDef		THOR_PERLIN_4D_VALUE_1_Param;
	PF_ParamDef		THOR_PERLIN_4D_VALUE_2_Param;
	PF_ParamDef		THOR_PERLIN_4D_VALUE_3_Param;
	PF_ParamDef		THOR_PERLIN_4D_VALUE_4_Param;
	PF_ParamDef		THOR_PERLIN_4D_POS_MULT_Param;
	PF_ParamDef		THOR_PERLIN_4D_MIX_Param;
	PF_ParamDef		THOR_PERLIN_4D_END_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_START_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_VALUE_1_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_VALUE_2_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_POS_MULT_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_MIX_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_END_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_START_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_VALUE_1_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_VALUE_2_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_VALUE_3_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_POS_MULT_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_MIX_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_END_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_START_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_VALUE_1_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_VALUE_2_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_VALUE_3_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_VALUE_4_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_POS_MULT_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_MIX_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_END_Param;
	PF_ParamDef		THOR_VIQ_2D_START_Param;
	PF_ParamDef		THOR_VIQ_2D_VALUE_1_Param;
	PF_ParamDef		THOR_VIQ_2D_VALUE_2_Param;
	PF_ParamDef		THOR_VIQ_2D_U_Param;
	PF_ParamDef		THOR_VIQ_2D_V_Param;
	PF_ParamDef		THOR_VIQ_2D_POS_MULT_Param;
	PF_ParamDef		THOR_VIQ_2D_MIX_Param;
	PF_ParamDef		THOR_VIQ_2D_END_Param;
	PF_ParamDef		THOR_VORONOI_2D_START_Param;
	PF_ParamDef		THOR_VORONOI_2D_VALUE_1_Param;
	PF_ParamDef		THOR_VORONOI_2D_VALUE_2_Param;
	PF_ParamDef		THOR_VORONOI_2D_POS_MULT_Param;
	PF_ParamDef		THOR_VORONOI_2D_MIX_Param;
	PF_ParamDef		THOR_VORONOI_2D_END_Param;
	PF_ParamDef		THOR_FRACTBROWN_1D_START_Param;
	PF_ParamDef		THOR_FRACTBROWN_1D_VALUE_1_Param;
	PF_ParamDef		THOR_FRACTBROWN_1D_POS_MULT_Param;
	PF_ParamDef		THOR_FRACTBROWN_1D_MIX_Param;
	PF_ParamDef		THOR_FRACTBROWN_1D_END_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_START_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_VALUE_1_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_VALUE_2_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_POS_MULT_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_MIX_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_END_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_START_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_VALUE_1_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_VALUE_2_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_VALUE_3_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_POS_MULT_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_MIX_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_END_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_START_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_VALUE_1_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_VALUE_2_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_VALUE_3_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_VALUE_4_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_POS_MULT_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_MIX_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_END_Param;
	PF_ParamDef		THOR_DISPLACE_START_Param;
	PF_ParamDef		THOR_GENERIC_1D_CB_Param;
	PF_ParamDef		THOR_GENERIC_2D_CB_Param;
	PF_ParamDef		THOR_GENERIC_3D_CB_Param;
	PF_ParamDef		THOR_PERLIN_2D_CB_Param;
	PF_ParamDef		THOR_PERLIN_3D_CB_Param;
	PF_ParamDef		THOR_PERLIN_4D_CB_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_CB_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_CB_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_CB_Param;
	PF_ParamDef		THOR_VIQ_2D_CB_Param;
	PF_ParamDef		THOR_VORONOI_2D_CB_Param;
	PF_ParamDef		THOR_FACTBROWN_1D_CB_Param;
	PF_ParamDef		THOR_FACTBROWN_2D_CB_Param;
	PF_ParamDef		THOR_FACTBROWN_3D_CB_Param;
	PF_ParamDef		THOR_FACTBROWN_4D_CB_Param;
	PF_ParamDef		THOR_DISPLACE_END_Param;

	PF_RenderRequest req = extra->input->output_request;
	PF_CheckoutResult in_result;


	AEFX_CLR_STRUCT(THOR_GENERIC_1D_START_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_1D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_1D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_1D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_1D_END_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_START_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_END_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_DIM_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_FREQ_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_START_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_END_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_START_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_VALUE_4_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_END_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_START_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_END_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_START_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_VALUE_4_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_END_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_U_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_V_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_1D_START_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_1D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_1D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_1D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_1D_END_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_START_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_END_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_START_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_VALUE_4_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_MIX_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_END_Param);
	AEFX_CLR_STRUCT(THOR_DISPLACE_START_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_1D_CB_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_CB_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_CB_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_CB_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_CB_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_CB_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_FACTBROWN_1D_CB_Param);
	AEFX_CLR_STRUCT(THOR_FACTBROWN_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_FACTBROWN_3D_CB_Param);
	AEFX_CLR_STRUCT(THOR_FACTBROWN_4D_CB_Param);
	AEFX_CLR_STRUCT(THOR_DISPLACE_END_Param);

	
	
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_DIM,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_DIM_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_FREQ,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_FREQ_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_VALUE_4,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_VALUE_4_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_VALUE_4,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_VALUE_4_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_U_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_U_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_V_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_V_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_1D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_1D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_1D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_1D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_1D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_1D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_1D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_1D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_1D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_1D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_VALUE_4,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_VALUE_4_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_DISPLACE_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_DISPLACE_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FACTBROWN_1D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FACTBROWN_1D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FACTBROWN_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FACTBROWN_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FACTBROWN_3D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FACTBROWN_3D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FACTBROWN_4D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FACTBROWN_4D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_DISPLACE_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_DISPLACE_END_Param));

	ERR(extra->cb->checkout_layer(in_data->effect_ref,
		THOR_INPUT,
		THOR_INPUT,
		&req,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&in_result));

	if (!err){
		UnionLRect(&in_result.result_rect, &extra->output->result_rect);
		UnionLRect(&in_result.max_result_rect, &extra->output->max_result_rect);
	}
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_DIM_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_FREQ_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_VALUE_4_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_VALUE_4_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_U_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_V_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_1D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_1D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_1D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_1D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_1D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_VALUE_4_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_DISPLACE_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FACTBROWN_1D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FACTBROWN_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FACTBROWN_3D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FACTBROWN_4D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_DISPLACE_END_Param));
	return err;
}

static PF_Err
SmartRender(
	PF_InData				*in_data,
	PF_OutData				*out_data,
	PF_SmartRenderExtra		*extra)
{
	PF_Err				err = PF_Err_NONE,
						err2 = PF_Err_NONE;

	PF_EffectWorld		*input_worldP = NULL,
						*output_worldP = NULL;
	PF_WorldSuite2		*wsP = NULL;
	PF_PixelFormat		format = PF_PixelFormat_INVALID;
	PF_FpLong			sliderVal = 0;

	AEGP_SuiteHandler suites(in_data->pica_basicP);

	PF_ParamDef		THOR_GENERIC_1D_START_Param;
	PF_ParamDef		THOR_GENERIC_1D_VALUE_1_Param;
	PF_ParamDef		THOR_GENERIC_1D_POS_MULT_Param;
	PF_ParamDef		THOR_GENERIC_1D_MIX_Param;
	PF_ParamDef		THOR_GENERIC_1D_END_Param;
	PF_ParamDef		THOR_GENERIC_2D_START_Param;
	PF_ParamDef		THOR_GENERIC_2D_VALUE_1_Param;
	PF_ParamDef		THOR_GENERIC_2D_VALUE_2_Param;
	PF_ParamDef		THOR_GENERIC_2D_POS_MULT_Param;
	PF_ParamDef		THOR_GENERIC_2D_MIX_Param;
	PF_ParamDef		THOR_GENERIC_2D_END_Param;
	PF_ParamDef		THOR_GENERIC_3D_START_Param;
	PF_ParamDef		THOR_GENERIC_3D_VALUE_1_Param;
	PF_ParamDef		THOR_GENERIC_3D_VALUE_2_Param;
	PF_ParamDef		THOR_GENERIC_3D_VALUE_3_Param;
	PF_ParamDef		THOR_GENERIC_3D_POS_MULT_Param;
	PF_ParamDef		THOR_GENERIC_3D_MIX_Param;
	PF_ParamDef		THOR_GENERIC_3D_END_Param;
	PF_ParamDef		THOR_PERLIN_2D_START_Param;
	PF_ParamDef		THOR_PERLIN_2D_VALUE_1_Param;
	PF_ParamDef		THOR_PERLIN_2D_VALUE_2_Param;
	PF_ParamDef		THOR_PERLIN_2D_DIM_Param;
	PF_ParamDef		THOR_PERLIN_2D_FREQ_Param;
	PF_ParamDef		THOR_PERLIN_2D_POS_MULT_Param;
	PF_ParamDef		THOR_PERLIN_2D_MIX_Param;
	PF_ParamDef		THOR_PERLIN_2D_END_Param;
	PF_ParamDef		THOR_PERLIN_3D_START_Param;
	PF_ParamDef		THOR_PERLIN_3D_VALUE_1_Param;
	PF_ParamDef		THOR_PERLIN_3D_VALUE_2_Param;
	PF_ParamDef		THOR_PERLIN_3D_VALUE_3_Param;
	PF_ParamDef		THOR_PERLIN_3D_POS_MULT_Param;
	PF_ParamDef		THOR_PERLIN_3D_MIX_Param;
	PF_ParamDef		THOR_PERLIN_3D_END_Param;
	PF_ParamDef		THOR_PERLIN_4D_START_Param;
	PF_ParamDef		THOR_PERLIN_4D_VALUE_1_Param;
	PF_ParamDef		THOR_PERLIN_4D_VALUE_2_Param;
	PF_ParamDef		THOR_PERLIN_4D_VALUE_3_Param;
	PF_ParamDef		THOR_PERLIN_4D_VALUE_4_Param;
	PF_ParamDef		THOR_PERLIN_4D_POS_MULT_Param;
	PF_ParamDef		THOR_PERLIN_4D_MIX_Param;
	PF_ParamDef		THOR_PERLIN_4D_END_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_START_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_VALUE_1_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_VALUE_2_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_POS_MULT_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_MIX_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_END_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_START_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_VALUE_1_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_VALUE_2_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_VALUE_3_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_POS_MULT_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_MIX_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_END_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_START_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_VALUE_1_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_VALUE_2_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_VALUE_3_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_VALUE_4_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_POS_MULT_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_MIX_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_END_Param;
	PF_ParamDef		THOR_VIQ_2D_START_Param;
	PF_ParamDef		THOR_VIQ_2D_VALUE_1_Param;
	PF_ParamDef		THOR_VIQ_2D_VALUE_2_Param;
	PF_ParamDef		THOR_VIQ_2D_U_Param;
	PF_ParamDef		THOR_VIQ_2D_V_Param;
	PF_ParamDef		THOR_VIQ_2D_POS_MULT_Param;
	PF_ParamDef		THOR_VIQ_2D_MIX_Param;
	PF_ParamDef		THOR_VIQ_2D_END_Param;
	PF_ParamDef		THOR_VORONOI_2D_START_Param;
	PF_ParamDef		THOR_VORONOI_2D_VALUE_1_Param;
	PF_ParamDef		THOR_VORONOI_2D_VALUE_2_Param;
	PF_ParamDef		THOR_VORONOI_2D_POS_MULT_Param;
	PF_ParamDef		THOR_VORONOI_2D_MIX_Param;
	PF_ParamDef		THOR_VORONOI_2D_END_Param;
	PF_ParamDef		THOR_FRACTBROWN_1D_START_Param;
	PF_ParamDef		THOR_FRACTBROWN_1D_VALUE_1_Param;
	PF_ParamDef		THOR_FRACTBROWN_1D_POS_MULT_Param;
	PF_ParamDef		THOR_FRACTBROWN_1D_MIX_Param;
	PF_ParamDef		THOR_FRACTBROWN_1D_END_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_START_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_VALUE_1_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_VALUE_2_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_POS_MULT_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_MIX_Param;
	PF_ParamDef		THOR_FRACTBROWN_2D_END_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_START_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_VALUE_1_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_VALUE_2_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_VALUE_3_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_POS_MULT_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_MIX_Param;
	PF_ParamDef		THOR_FRACTBROWN_3D_END_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_START_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_VALUE_1_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_VALUE_2_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_VALUE_3_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_VALUE_4_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_POS_MULT_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_MIX_Param;
	PF_ParamDef		THOR_FRACTBROWN_IQ_END_Param;
	PF_ParamDef		THOR_DISPLACE_START_Param;
	PF_ParamDef		THOR_GENERIC_1D_CB_Param;
	PF_ParamDef		THOR_GENERIC_2D_CB_Param;
	PF_ParamDef		THOR_GENERIC_3D_CB_Param;
	PF_ParamDef		THOR_PERLIN_2D_CB_Param;
	PF_ParamDef		THOR_PERLIN_3D_CB_Param;
	PF_ParamDef		THOR_PERLIN_4D_CB_Param;
	PF_ParamDef		THOR_SIMPLEX_2D_CB_Param;
	PF_ParamDef		THOR_SIMPLEX_3D_CB_Param;
	PF_ParamDef		THOR_SIMPLEX_4D_CB_Param;
	PF_ParamDef		THOR_VIQ_2D_CB_Param;
	PF_ParamDef		THOR_VORONOI_2D_CB_Param;
	PF_ParamDef		THOR_FACTBROWN_1D_CB_Param;
	PF_ParamDef		THOR_FACTBROWN_2D_CB_Param;
	PF_ParamDef		THOR_FACTBROWN_3D_CB_Param;
	PF_ParamDef		THOR_FACTBROWN_4D_CB_Param;
	PF_ParamDef		THOR_DISPLACE_END_Param;

	AEFX_CLR_STRUCT(THOR_GENERIC_1D_START_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_1D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_1D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_1D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_1D_END_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_START_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_END_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_DIM_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_FREQ_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_START_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_END_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_START_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_VALUE_4_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_END_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_START_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_END_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_START_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_VALUE_4_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_END_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_U_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_V_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_1D_START_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_1D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_1D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_1D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_1D_END_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_START_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_2D_END_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_START_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_MIX_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_3D_END_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_START_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_VALUE_1_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_VALUE_2_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_VALUE_3_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_VALUE_4_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_POS_MULT_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_MIX_Param);
	AEFX_CLR_STRUCT(THOR_FRACTBROWN_IQ_END_Param);
	AEFX_CLR_STRUCT(THOR_DISPLACE_START_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_1D_CB_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_GENERIC_3D_CB_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_3D_CB_Param);
	AEFX_CLR_STRUCT(THOR_PERLIN_4D_CB_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_3D_CB_Param);
	AEFX_CLR_STRUCT(THOR_SIMPLEX_4D_CB_Param);
	AEFX_CLR_STRUCT(THOR_VIQ_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_VORONOI_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_FACTBROWN_1D_CB_Param);
	AEFX_CLR_STRUCT(THOR_FACTBROWN_2D_CB_Param);
	AEFX_CLR_STRUCT(THOR_FACTBROWN_3D_CB_Param);
	AEFX_CLR_STRUCT(THOR_FACTBROWN_4D_CB_Param);
	AEFX_CLR_STRUCT(THOR_DISPLACE_END_Param);
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_DIM,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_DIM_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_FREQ,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_FREQ_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_VALUE_4,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_VALUE_4_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_VALUE_4,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_VALUE_4_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_U_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_U_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_V_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_V_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_1D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_1D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_1D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_1D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_1D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_1D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_1D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_1D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_1D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_1D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_2D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_2D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_3D_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_3D_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_VALUE_1,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_VALUE_1_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_VALUE_2,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_VALUE_2_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_VALUE_3,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_VALUE_3_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_VALUE_4,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_VALUE_4_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_POS_MULT,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_POS_MULT_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_MIX,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_MIX_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FRACTBROWN_IQ_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FRACTBROWN_IQ_END_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_DISPLACE_START,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_DISPLACE_START_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_1D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_1D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_GENERIC_3D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_GENERIC_3D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_3D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_3D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_PERLIN_4D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_PERLIN_4D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_3D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_3D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_SIMPLEX_4D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_SIMPLEX_4D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VIQ_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VIQ_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_VORONOI_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_VORONOI_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FACTBROWN_1D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FACTBROWN_1D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FACTBROWN_2D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FACTBROWN_2D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FACTBROWN_3D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FACTBROWN_3D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_FACTBROWN_4D_CB,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_FACTBROWN_4D_CB_Param));
	ERR(PF_CHECKOUT_PARAM(in_data,
		THOR_DISPLACE_END,
		in_data->current_time,
		in_data->time_step,
		in_data->time_scale,
		&THOR_DISPLACE_END_Param));




	PF_FpLong		THOR_GENERIC_1D_START_Val;
	PF_FpLong		THOR_GENERIC_1D_VALUE_1_Val;
	PF_FpLong		THOR_GENERIC_1D_POS_MULT_Val;
	PF_FpLong		THOR_GENERIC_1D_MIX_Val;
	PF_FpLong		THOR_GENERIC_1D_END_Val;
	PF_FpLong		THOR_GENERIC_2D_START_Val;
	PF_FpLong		THOR_GENERIC_2D_VALUE_1_Val;
	PF_FpLong		THOR_GENERIC_2D_VALUE_2_Val;
	PF_FpLong		THOR_GENERIC_2D_POS_MULT_Val;
	PF_FpLong		THOR_GENERIC_2D_MIX_Val;
	PF_FpLong		THOR_GENERIC_2D_END_Val;
	PF_FpLong		THOR_GENERIC_3D_START_Val;
	PF_FpLong		THOR_GENERIC_3D_VALUE_1_Val;
	PF_FpLong		THOR_GENERIC_3D_VALUE_2_Val;
	PF_FpLong		THOR_GENERIC_3D_VALUE_3_Val;
	PF_FpLong		THOR_GENERIC_3D_POS_MULT_Val;
	PF_FpLong		THOR_GENERIC_3D_MIX_Val;
	PF_FpLong		THOR_GENERIC_3D_END_Val;
	PF_FpLong		THOR_PERLIN_2D_START_Val;
	PF_FpLong		THOR_PERLIN_2D_VALUE_1_Val;
	PF_FpLong		THOR_PERLIN_2D_VALUE_2_Val;
	PF_FpLong		THOR_PERLIN_2D_DIM_Val;
	PF_FpLong		THOR_PERLIN_2D_FREQ_Val;
	PF_FpLong		THOR_PERLIN_2D_POS_MULT_Val;
	PF_FpLong		THOR_PERLIN_2D_MIX_Val;
	PF_FpLong		THOR_PERLIN_2D_END_Val;
	PF_FpLong		THOR_PERLIN_3D_START_Val;
	PF_FpLong		THOR_PERLIN_3D_VALUE_1_Val;
	PF_FpLong		THOR_PERLIN_3D_VALUE_2_Val;
	PF_FpLong		THOR_PERLIN_3D_VALUE_3_Val;
	PF_FpLong		THOR_PERLIN_3D_POS_MULT_Val;
	PF_FpLong		THOR_PERLIN_3D_MIX_Val;
	PF_FpLong		THOR_PERLIN_3D_END_Val;
	PF_FpLong		THOR_PERLIN_4D_START_Val;
	PF_FpLong		THOR_PERLIN_4D_VALUE_1_Val;
	PF_FpLong		THOR_PERLIN_4D_VALUE_2_Val;
	PF_FpLong		THOR_PERLIN_4D_VALUE_3_Val;
	PF_FpLong		THOR_PERLIN_4D_VALUE_4_Val;
	PF_FpLong		THOR_PERLIN_4D_POS_MULT_Val;
	PF_FpLong		THOR_PERLIN_4D_MIX_Val;
	PF_FpLong		THOR_PERLIN_4D_END_Val;
	PF_FpLong		THOR_SIMPLEX_2D_START_Val;
	PF_FpLong		THOR_SIMPLEX_2D_VALUE_1_Val;
	PF_FpLong		THOR_SIMPLEX_2D_VALUE_2_Val;
	PF_FpLong		THOR_SIMPLEX_2D_POS_MULT_Val;
	PF_FpLong		THOR_SIMPLEX_2D_MIX_Val;
	PF_FpLong		THOR_SIMPLEX_2D_END_Val;
	PF_FpLong		THOR_SIMPLEX_3D_START_Val;
	PF_FpLong		THOR_SIMPLEX_3D_VALUE_1_Val;
	PF_FpLong		THOR_SIMPLEX_3D_VALUE_2_Val;
	PF_FpLong		THOR_SIMPLEX_3D_VALUE_3_Val;
	PF_FpLong		THOR_SIMPLEX_3D_POS_MULT_Val;
	PF_FpLong		THOR_SIMPLEX_3D_MIX_Val;
	PF_FpLong		THOR_SIMPLEX_3D_END_Val;
	PF_FpLong		THOR_SIMPLEX_4D_START_Val;
	PF_FpLong		THOR_SIMPLEX_4D_VALUE_1_Val;
	PF_FpLong		THOR_SIMPLEX_4D_VALUE_2_Val;
	PF_FpLong		THOR_SIMPLEX_4D_VALUE_3_Val;
	PF_FpLong		THOR_SIMPLEX_4D_VALUE_4_Val;
	PF_FpLong		THOR_SIMPLEX_4D_POS_MULT_Val;
	PF_FpLong		THOR_SIMPLEX_4D_MIX_Val;
	PF_FpLong		THOR_SIMPLEX_4D_END_Val;
	PF_FpLong		THOR_VIQ_2D_START_Val;
	PF_FpLong		THOR_VIQ_2D_VALUE_1_Val;
	PF_FpLong		THOR_VIQ_2D_VALUE_2_Val;
	PF_FpLong		THOR_VIQ_2D_U_Val;
	PF_FpLong		THOR_VIQ_2D_V_Val;
	PF_FpLong		THOR_VIQ_2D_POS_MULT_Val;
	PF_FpLong		THOR_VIQ_2D_MIX_Val;
	PF_FpLong		THOR_VIQ_2D_END_Val;
	PF_FpLong		THOR_VORONOI_2D_START_Val;
	PF_FpLong		THOR_VORONOI_2D_VALUE_1_Val;
	PF_FpLong		THOR_VORONOI_2D_VALUE_2_Val;
	PF_FpLong		THOR_VORONOI_2D_POS_MULT_Val;
	PF_FpLong		THOR_VORONOI_2D_MIX_Val;
	PF_FpLong		THOR_VORONOI_2D_END_Val;
	PF_FpLong		THOR_FRACTBROWN_1D_START_Val;
	PF_FpLong		THOR_FRACTBROWN_1D_VALUE_1_Val;
	PF_FpLong		THOR_FRACTBROWN_1D_POS_MULT_Val;
	PF_FpLong		THOR_FRACTBROWN_1D_MIX_Val;
	PF_FpLong		THOR_FRACTBROWN_1D_END_Val;
	PF_FpLong		THOR_FRACTBROWN_2D_START_Val;
	PF_FpLong		THOR_FRACTBROWN_2D_VALUE_1_Val;
	PF_FpLong		THOR_FRACTBROWN_2D_VALUE_2_Val;
	PF_FpLong		THOR_FRACTBROWN_2D_POS_MULT_Val;
	PF_FpLong		THOR_FRACTBROWN_2D_MIX_Val;
	PF_FpLong		THOR_FRACTBROWN_2D_END_Val;
	PF_FpLong		THOR_FRACTBROWN_3D_START_Val;
	PF_FpLong		THOR_FRACTBROWN_3D_VALUE_1_Val;
	PF_FpLong		THOR_FRACTBROWN_3D_VALUE_2_Val;
	PF_FpLong		THOR_FRACTBROWN_3D_VALUE_3_Val;
	PF_FpLong		THOR_FRACTBROWN_3D_POS_MULT_Val;
	PF_FpLong		THOR_FRACTBROWN_3D_MIX_Val;
	PF_FpLong		THOR_FRACTBROWN_3D_END_Val;
	PF_FpLong		THOR_FRACTBROWN_IQ_START_Val;
	PF_FpLong		THOR_FRACTBROWN_IQ_VALUE_1_Val;
	PF_FpLong		THOR_FRACTBROWN_IQ_VALUE_2_Val;
	PF_FpLong		THOR_FRACTBROWN_IQ_VALUE_3_Val;
	PF_FpLong		THOR_FRACTBROWN_IQ_VALUE_4_Val;
	PF_FpLong		THOR_FRACTBROWN_IQ_POS_MULT_Val;
	PF_FpLong		THOR_FRACTBROWN_IQ_MIX_Val;
	PF_FpLong		THOR_FRACTBROWN_IQ_END_Val;
	PF_FpLong		THOR_DISPLACE_START_Val;
	PF_FpLong		THOR_GENERIC_1D_CB_Val;
	PF_FpLong		THOR_GENERIC_2D_CB_Val;
	PF_FpLong		THOR_GENERIC_3D_CB_Val;
	PF_FpLong		THOR_PERLIN_2D_CB_Val;
	PF_FpLong		THOR_PERLIN_3D_CB_Val;
	PF_FpLong		THOR_PERLIN_4D_CB_Val;
	PF_FpLong		THOR_SIMPLEX_2D_CB_Val;
	PF_FpLong		THOR_SIMPLEX_3D_CB_Val;
	PF_FpLong		THOR_SIMPLEX_4D_CB_Val;
	PF_FpLong		THOR_VIQ_2D_CB_Val;
	PF_FpLong		THOR_VORONOI_2D_CB_Val;
	PF_FpLong		THOR_FACTBROWN_1D_CB_Val;
	PF_FpLong		THOR_FACTBROWN_2D_CB_Val;
	PF_FpLong		THOR_FACTBROWN_3D_CB_Val;
	PF_FpLong		THOR_FACTBROWN_4D_CB_Val;
	PF_FpLong		THOR_DISPLACE_END_Val;




	if (!err){

		THOR_GENERIC_1D_VALUE_1_Val = THOR_GENERIC_1D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_GENERIC_1D_POS_MULT_Val = THOR_GENERIC_1D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_GENERIC_1D_MIX_Val = THOR_GENERIC_1D_MIX_Param.u.fs_d.value / 100.00;


		THOR_GENERIC_2D_VALUE_1_Val = THOR_GENERIC_2D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_GENERIC_2D_VALUE_2_Val = THOR_GENERIC_2D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_GENERIC_2D_POS_MULT_Val = THOR_GENERIC_2D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_GENERIC_2D_MIX_Val = THOR_GENERIC_2D_MIX_Param.u.fs_d.value / 100.00;


		THOR_GENERIC_3D_VALUE_1_Val = THOR_GENERIC_3D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_GENERIC_3D_VALUE_2_Val = THOR_GENERIC_3D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_GENERIC_3D_VALUE_3_Val = THOR_GENERIC_3D_VALUE_3_Param.u.fs_d.value / 100.00;
		THOR_GENERIC_3D_POS_MULT_Val = THOR_GENERIC_3D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_GENERIC_3D_MIX_Val = THOR_GENERIC_3D_MIX_Param.u.fs_d.value / 100.00;


		THOR_PERLIN_2D_VALUE_1_Val = THOR_PERLIN_2D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_PERLIN_2D_VALUE_2_Val = THOR_PERLIN_2D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_PERLIN_2D_DIM_Val = THOR_PERLIN_2D_DIM_Param.u.fs_d.value / 100.00;
		THOR_PERLIN_2D_FREQ_Val = THOR_PERLIN_2D_FREQ_Param.u.fs_d.value / 100.00;
		THOR_PERLIN_2D_POS_MULT_Val = THOR_PERLIN_2D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_PERLIN_2D_MIX_Val = THOR_PERLIN_2D_MIX_Param.u.fs_d.value / 100.00;


		THOR_PERLIN_3D_VALUE_1_Val = THOR_PERLIN_3D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_PERLIN_3D_VALUE_2_Val = THOR_PERLIN_3D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_PERLIN_3D_VALUE_3_Val = THOR_PERLIN_3D_VALUE_3_Param.u.fs_d.value / 100.00;
		THOR_PERLIN_3D_POS_MULT_Val = THOR_PERLIN_3D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_PERLIN_3D_MIX_Val = THOR_PERLIN_3D_MIX_Param.u.fs_d.value / 100.00;


		THOR_PERLIN_4D_VALUE_1_Val = THOR_PERLIN_4D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_PERLIN_4D_VALUE_2_Val = THOR_PERLIN_4D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_PERLIN_4D_VALUE_3_Val = THOR_PERLIN_4D_VALUE_3_Param.u.fs_d.value / 100.00;
		THOR_PERLIN_4D_VALUE_4_Val = THOR_PERLIN_4D_VALUE_4_Param.u.fs_d.value / 100.00;
		THOR_PERLIN_4D_POS_MULT_Val = THOR_PERLIN_4D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_PERLIN_4D_MIX_Val = THOR_PERLIN_4D_MIX_Param.u.fs_d.value / 100.00;


		THOR_SIMPLEX_2D_VALUE_1_Val = THOR_SIMPLEX_2D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_SIMPLEX_2D_VALUE_2_Val = THOR_SIMPLEX_2D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_SIMPLEX_2D_POS_MULT_Val = THOR_SIMPLEX_2D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_SIMPLEX_2D_MIX_Val = THOR_SIMPLEX_2D_MIX_Param.u.fs_d.value / 100.00;


		THOR_SIMPLEX_3D_VALUE_1_Val = THOR_SIMPLEX_3D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_SIMPLEX_3D_VALUE_2_Val = THOR_SIMPLEX_3D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_SIMPLEX_3D_VALUE_3_Val = THOR_SIMPLEX_3D_VALUE_3_Param.u.fs_d.value / 100.00;
		THOR_SIMPLEX_3D_POS_MULT_Val = THOR_SIMPLEX_3D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_SIMPLEX_3D_MIX_Val = THOR_SIMPLEX_3D_MIX_Param.u.fs_d.value / 100.00;


		THOR_SIMPLEX_4D_VALUE_1_Val = THOR_SIMPLEX_4D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_SIMPLEX_4D_VALUE_2_Val = THOR_SIMPLEX_4D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_SIMPLEX_4D_VALUE_3_Val = THOR_SIMPLEX_4D_VALUE_3_Param.u.fs_d.value / 100.00;
		THOR_SIMPLEX_4D_VALUE_4_Val = THOR_SIMPLEX_4D_VALUE_4_Param.u.fs_d.value / 100.00;
		THOR_SIMPLEX_4D_POS_MULT_Val = THOR_SIMPLEX_4D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_SIMPLEX_4D_MIX_Val = THOR_SIMPLEX_4D_MIX_Param.u.fs_d.value / 100.00;


		THOR_VIQ_2D_VALUE_1_Val = THOR_VIQ_2D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_VIQ_2D_VALUE_2_Val = THOR_VIQ_2D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_VIQ_2D_U_Val = THOR_VIQ_2D_U_Param.u.fs_d.value / 100.00;
		THOR_VIQ_2D_V_Val = THOR_VIQ_2D_V_Param.u.fs_d.value / 100.00;
		THOR_VIQ_2D_POS_MULT_Val = THOR_VIQ_2D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_VIQ_2D_MIX_Val = THOR_VIQ_2D_MIX_Param.u.fs_d.value / 100.00;


		THOR_VORONOI_2D_VALUE_1_Val = THOR_VORONOI_2D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_VORONOI_2D_VALUE_2_Val = THOR_VORONOI_2D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_VORONOI_2D_POS_MULT_Val = THOR_VORONOI_2D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_VORONOI_2D_MIX_Val = THOR_VORONOI_2D_MIX_Param.u.fs_d.value / 100.00;


		THOR_FRACTBROWN_1D_VALUE_1_Val = THOR_FRACTBROWN_1D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_FRACTBROWN_1D_POS_MULT_Val = THOR_FRACTBROWN_1D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_FRACTBROWN_1D_MIX_Val = THOR_FRACTBROWN_1D_MIX_Param.u.fs_d.value / 100.00;


		THOR_FRACTBROWN_2D_VALUE_1_Val = THOR_FRACTBROWN_2D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_FRACTBROWN_2D_VALUE_2_Val = THOR_FRACTBROWN_2D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_FRACTBROWN_2D_POS_MULT_Val = THOR_FRACTBROWN_2D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_FRACTBROWN_2D_MIX_Val = THOR_FRACTBROWN_2D_MIX_Param.u.fs_d.value / 100.00;


		THOR_FRACTBROWN_3D_VALUE_1_Val = THOR_FRACTBROWN_3D_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_FRACTBROWN_3D_VALUE_2_Val = THOR_FRACTBROWN_3D_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_FRACTBROWN_3D_VALUE_3_Val = THOR_FRACTBROWN_3D_VALUE_3_Param.u.fs_d.value / 100.00;
		THOR_FRACTBROWN_3D_POS_MULT_Val = THOR_FRACTBROWN_3D_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_FRACTBROWN_3D_MIX_Val = THOR_FRACTBROWN_3D_MIX_Param.u.fs_d.value / 100.00;


		THOR_FRACTBROWN_IQ_VALUE_1_Val = THOR_FRACTBROWN_IQ_VALUE_1_Param.u.fs_d.value / 100.00;
		THOR_FRACTBROWN_IQ_VALUE_2_Val = THOR_FRACTBROWN_IQ_VALUE_2_Param.u.fs_d.value / 100.00;
		THOR_FRACTBROWN_IQ_VALUE_3_Val = THOR_FRACTBROWN_IQ_VALUE_3_Param.u.fs_d.value / 100.00;
		THOR_FRACTBROWN_IQ_VALUE_4_Val = THOR_FRACTBROWN_IQ_VALUE_4_Param.u.fs_d.value / 100.00;
		THOR_FRACTBROWN_IQ_POS_MULT_Val = THOR_FRACTBROWN_IQ_POS_MULT_Param.u.fs_d.value / 10.00;
		THOR_FRACTBROWN_IQ_MIX_Val = THOR_FRACTBROWN_IQ_MIX_Param.u.fs_d.value / 100.00;


		THOR_GENERIC_1D_CB_Val = bool2float(THOR_GENERIC_1D_CB_Param.u.bd.value);
		THOR_GENERIC_2D_CB_Val = bool2float(THOR_GENERIC_2D_CB_Param.u.bd.value);
		THOR_GENERIC_3D_CB_Val = bool2float(THOR_GENERIC_3D_CB_Param.u.bd.value);
		THOR_PERLIN_2D_CB_Val = bool2float(THOR_PERLIN_2D_CB_Param.u.bd.value);
		THOR_PERLIN_3D_CB_Val = bool2float(THOR_PERLIN_3D_CB_Param.u.bd.value);
		THOR_PERLIN_4D_CB_Val = bool2float(THOR_PERLIN_4D_CB_Param.u.bd.value);
		THOR_SIMPLEX_2D_CB_Val = bool2float(THOR_SIMPLEX_2D_CB_Param.u.bd.value);
		THOR_SIMPLEX_3D_CB_Val = bool2float(THOR_SIMPLEX_3D_CB_Param.u.bd.value);
		THOR_SIMPLEX_4D_CB_Val = bool2float(THOR_SIMPLEX_4D_CB_Param.u.bd.value);
		THOR_VIQ_2D_CB_Val = bool2float(THOR_VIQ_2D_CB_Param.u.bd.value);
		THOR_VORONOI_2D_CB_Val = bool2float(THOR_VORONOI_2D_CB_Param.u.bd.value);
		THOR_FACTBROWN_1D_CB_Val = bool2float(THOR_FACTBROWN_1D_CB_Param.u.bd.value);
		THOR_FACTBROWN_2D_CB_Val = bool2float(THOR_FACTBROWN_2D_CB_Param.u.bd.value);
		THOR_FACTBROWN_3D_CB_Val = bool2float(THOR_FACTBROWN_3D_CB_Param.u.bd.value);
		THOR_FACTBROWN_4D_CB_Val = bool2float(THOR_FACTBROWN_4D_CB_Param.u.bd.value);
	}

	ERR((extra->cb->checkout_layer_pixels(in_data->effect_ref, THOR_INPUT, &input_worldP)));

	ERR(extra->cb->checkout_output(in_data->effect_ref, &output_worldP));

	ERR(AEFX_AcquireSuite(in_data,
		out_data,
		kPFWorldSuite,
		kPFWorldSuiteVersion2,
		"Couldn't load suite.",
		(void**)&wsP));

	if (!err){
		try
		{
			// always restore back AE's own OGL context
			SaveRestoreOGLContext oSavedContext;

			// our render specific context (one per thread)
			AESDK_OpenGL::AESDK_OpenGL_EffectRenderDataPtr renderContext = GetCurrentRenderContext();

			if (!renderContext->mInitialized) {
				//Now comes the OpenGL part - OS specific loading to start with
				AESDK_OpenGL_Startup(*renderContext.get(), S_GLator_EffectCommonData.get());

				renderContext->mInitialized = true;
			}

			renderContext->SetPluginContext();
			
			// - Gremedy OpenGL debugger
			// - Example of using a OpenGL extension
			bool hasGremedy = renderContext->mExtensions.find(gl::GLextension::GL_GREMEDY_frame_terminator) != renderContext->mExtensions.end();

			A_long				widthL = input_worldP->width;
			A_long				heightL = input_worldP->height;

			//loading OpenGL resources
			AESDK_OpenGL_InitResources(*renderContext.get(), widthL, heightL, S_ResourcePath);

			CHECK(wsP->PF_GetPixelFormat(input_worldP, &format));

			// upload the input world to a texture
			size_t pixSize;
			gl::GLenum glFmt;
			float multiplier16bit;
			gl::GLuint inputFrameTexture = UploadTexture(suites, format, input_worldP, output_worldP, in_data, pixSize, glFmt, multiplier16bit);
			
			// Set up the frame-buffer object just like a window.
			AESDK_OpenGL_MakeReadyToRender(*renderContext.get(), renderContext->mOutputFrameTexture);
			ReportIfErrorFramebuffer(in_data, out_data);

			glViewport(0, 0, widthL, heightL);
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			
			// - simply blend the texture inside the frame buffer
			// - TODO: hack your own shader there
			RenderGL(renderContext, widthL, heightL, inputFrameTexture, sliderVal, multiplier16bit);

			// - we toggle PBO textures (we use the PBO we just created as an input)
			AESDK_OpenGL_MakeReadyToRender(*renderContext.get(), inputFrameTexture);
			ReportIfErrorFramebuffer(in_data, out_data);

			glClear(GL_COLOR_BUFFER_BIT);

			// swizzle using the previous output
			SwizzleGL(renderContext, widthL, heightL, renderContext->mOutputFrameTexture, multiplier16bit);

			if (hasGremedy) {
				gl::glFrameTerminatorGREMEDY();
			}

			// - get back to CPU the result, and inside the output world
			DownloadTexture(renderContext, suites, input_worldP, output_worldP, in_data,
				format, pixSize, glFmt);

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
			glDeleteTextures(1, &inputFrameTexture);
		}
		catch (PF_Err& thrown_err)
		{
			err = thrown_err;
		}
		catch (...)
		{
			err = PF_Err_OUT_OF_MEMORY;
		}
	}

	// If you have PF_ABORT or PF_PROG higher up, you must set
	// the AE context back before calling them, and then take it back again
	// if you want to call some more OpenGL.		
	ERR(PF_ABORT(in_data));

	ERR2(AEFX_ReleaseSuite(in_data,
		out_data,
		kPFWorldSuite,
		kPFWorldSuiteVersion2,
		"Couldn't release suite."));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_DIM_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_FREQ_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_VALUE_4_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_VALUE_4_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_U_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_V_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_1D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_1D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_1D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_1D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_1D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_2D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_3D_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_VALUE_1_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_VALUE_2_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_VALUE_3_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_VALUE_4_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_POS_MULT_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_MIX_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FRACTBROWN_IQ_END_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_DISPLACE_START_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_1D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_GENERIC_3D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_3D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_PERLIN_4D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_3D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_SIMPLEX_4D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VIQ_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_VORONOI_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FACTBROWN_1D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FACTBROWN_2D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FACTBROWN_3D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_FACTBROWN_4D_CB_Param));
	ERR2(PF_CHECKIN_PARAM(in_data, &THOR_DISPLACE_END_Param));
	ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, THOR_INPUT));

	return err;
}


extern "C" DllExport
PF_Err PluginDataEntryFunction(
	PF_PluginDataPtr inPtr,
	PF_PluginDataCB inPluginDataCallBackPtr,
	SPBasicSuite* inSPBasicSuitePtr,
	const char* inHostName,
	const char* inHostVersion)
{
	PF_Err result = PF_Err_INVALID_CALLBACK;

	result = PF_REGISTER_EFFECT(
		inPtr,
		inPluginDataCallBackPtr,
		"GLator", // Name
		"ADBE GLator", // Match Name
		"Sample Plug-ins", // Category
		AE_RESERVED_INFO); // Reserved Info

	return result;
}


PF_Err
EffectMain(
	PF_Cmd			cmd,
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output,
	void			*extra)
{
	PF_Err		err = PF_Err_NONE;
	
	try {
		switch (cmd) {
			case PF_Cmd_ABOUT:
				err = About(in_data,
							out_data,
							params,
							output);
				break;
				
			case PF_Cmd_GLOBAL_SETUP:
				err = GlobalSetup(	in_data,
									out_data,
									params,
									output);
				break;
				
			case PF_Cmd_PARAMS_SETUP:
				err = ParamsSetup(	in_data,
									out_data,
									params,
									output);
				break;
				
			case PF_Cmd_GLOBAL_SETDOWN:
				err = GlobalSetdown(	in_data,
										out_data,
										params,
										output);
				break;

			case  PF_Cmd_SMART_PRE_RENDER:
				err = PreRender(in_data, out_data, reinterpret_cast<PF_PreRenderExtra*>(extra));
				break;

			case  PF_Cmd_SMART_RENDER:
				err = SmartRender(in_data, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra));
				break;
		}
	}
	catch(PF_Err &thrown_err){
		err = thrown_err;
	}
	return err;
}
