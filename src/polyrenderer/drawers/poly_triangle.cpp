/*
**  Triangle drawers
**  Copyright (c) 2016 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#include <stddef.h>
#include "templates.h"
#include "doomdef.h"
#include "i_system.h"
#include "w_wad.h"
#include "v_video.h"
#include "doomstat.h"
#include "st_stuff.h"
#include "g_game.h"
#include "g_level.h"
#include "r_data/r_translate.h"
#include "v_palette.h"
#include "r_data/colormaps.h"
#include "poly_triangle.h"
#include "polyrenderer/poly_renderer.h"
#include "swrenderer/drawers/r_draw_rgba.h"
#include "screen_triangle.h"
#include "x86.h"

int PolyTriangleDrawer::viewport_x;
int PolyTriangleDrawer::viewport_y;
int PolyTriangleDrawer::viewport_width;
int PolyTriangleDrawer::viewport_height;
int PolyTriangleDrawer::dest_pitch;
int PolyTriangleDrawer::dest_width;
int PolyTriangleDrawer::dest_height;
uint8_t *PolyTriangleDrawer::dest;
bool PolyTriangleDrawer::dest_bgra;
bool PolyTriangleDrawer::mirror;

void PolyTriangleDrawer::set_viewport(int x, int y, int width, int height, DCanvas *canvas)
{
	dest = (uint8_t*)canvas->GetBuffer();
	dest_width = canvas->GetWidth();
	dest_height = canvas->GetHeight();
	dest_pitch = canvas->GetPitch();
	dest_bgra = canvas->IsBgra();

	int offsetx = clamp(x, 0, dest_width);
	int offsety = clamp(y, 0, dest_height);
	int pixelsize = dest_bgra ? 4 : 1;

	viewport_x = x - offsetx;
	viewport_y = y - offsety;
	viewport_width = width;
	viewport_height = height;

	dest += (offsetx + offsety * dest_pitch) * pixelsize;
	dest_width = clamp(viewport_x + viewport_width, 0, dest_width - offsetx);
	dest_height = clamp(viewport_y + viewport_height, 0, dest_height - offsety);

	mirror = false;
}

void PolyTriangleDrawer::toggle_mirror()
{
	mirror = !mirror;
}

bool PolyTriangleDrawer::is_mirror()
{
	return mirror;
}

void PolyTriangleDrawer::draw_arrays(const PolyDrawArgs &drawargs, WorkerThreadData *thread)
{
	if (drawargs.VertexCount() < 3)
		return;

	TriDrawTriangleArgs args;
	args.dest = dest;
	args.pitch = dest_pitch;
	args.clipright = dest_width;
	args.clipbottom = dest_height;
	args.uniforms = &drawargs;
	args.destBgra = dest_bgra;
	args.stencilPitch = PolyStencilBuffer::Instance()->BlockWidth();
	args.stencilValues = PolyStencilBuffer::Instance()->Values();
	args.stencilMasks = PolyStencilBuffer::Instance()->Masks();
	args.subsectorGBuffer = PolySubsectorGBuffer::Instance()->Values();
	args.zbuffer = PolyZBuffer::Instance()->Values();

	bool ccw = drawargs.FaceCullCCW();
	const TriVertex *vinput = drawargs.Vertices();
	int vcount = drawargs.VertexCount();

	ShadedTriVertex vert[3];
	if (drawargs.DrawMode() == PolyDrawMode::Triangles)
	{
		for (int i = 0; i < vcount / 3; i++)
		{
			for (int j = 0; j < 3; j++)
				vert[j] = shade_vertex(*drawargs.ObjectToClip(), drawargs.ClipPlane(), *(vinput++));
			draw_shaded_triangle(vert, ccw, &args, thread);
		}
	}
	else if (drawargs.DrawMode() == PolyDrawMode::TriangleFan)
	{
		vert[0] = shade_vertex(*drawargs.ObjectToClip(), drawargs.ClipPlane(), *(vinput++));
		vert[1] = shade_vertex(*drawargs.ObjectToClip(), drawargs.ClipPlane(), *(vinput++));
		for (int i = 2; i < vcount; i++)
		{
			vert[2] = shade_vertex(*drawargs.ObjectToClip(), drawargs.ClipPlane(), *(vinput++));
			draw_shaded_triangle(vert, ccw, &args, thread);
			vert[1] = vert[2];
		}
	}
	else // TriangleDrawMode::TriangleStrip
	{
		vert[0] = shade_vertex(*drawargs.ObjectToClip(), drawargs.ClipPlane(), *(vinput++));
		vert[1] = shade_vertex(*drawargs.ObjectToClip(), drawargs.ClipPlane(), *(vinput++));
		for (int i = 2; i < vcount; i++)
		{
			vert[2] = shade_vertex(*drawargs.ObjectToClip(), drawargs.ClipPlane(), *(vinput++));
			draw_shaded_triangle(vert, ccw, &args, thread);
			vert[0] = vert[1];
			vert[1] = vert[2];
			ccw = !ccw;
		}
	}
}

ShadedTriVertex PolyTriangleDrawer::shade_vertex(const TriMatrix &objectToClip, const float *clipPlane, const TriVertex &v)
{
	// Apply transform to get clip coordinates:
	ShadedTriVertex sv = objectToClip * v;

	// Calculate gl_ClipDistance[0]
	sv.clipDistance0 = v.x * clipPlane[0] + v.y * clipPlane[1] + v.z * clipPlane[2] + v.w * clipPlane[3];

	return sv;
}

bool PolyTriangleDrawer::is_degenerate(const ShadedTriVertex *vert)
{
	// A degenerate triangle has a zero cross product for two of its sides.
	float ax = vert[1].x - vert[0].x;
	float ay = vert[1].y - vert[0].y;
	float az = vert[1].w - vert[0].w;
	float bx = vert[2].x - vert[0].x;
	float by = vert[2].y - vert[0].y;
	float bz = vert[2].w - vert[0].w;
	float crossx = ay * bz - az * by;
	float crossy = az * bx - ax * bz;
	float crossz = ax * by - ay * bx;
	float crosslengthsqr = crossx * crossx + crossy * crossy + crossz * crossz;
	return crosslengthsqr <= 1.e-6f;
}

void PolyTriangleDrawer::draw_shaded_triangle(const ShadedTriVertex *vert, bool ccw, TriDrawTriangleArgs *args, WorkerThreadData *thread)
{
	// Reject triangle if degenerate
	if (is_degenerate(vert))
		return;

	// Cull, clip and generate additional vertices as needed
	TriVertex clippedvert[max_additional_vertices];
	int numclipvert = clipedge(vert, clippedvert);

#ifdef NO_SSE
	// Map to 2D viewport:
	for (int j = 0; j < numclipvert; j++)
	{
		auto &v = clippedvert[j];

		// Calculate normalized device coordinates:
		v.w = 1.0f / v.w;
		v.x *= v.w;
		v.y *= v.w;
		v.z *= v.w;

		// Apply viewport scale to get screen coordinates:
		v.x = viewport_x + viewport_width * (1.0f + v.x) * 0.5f;
		v.y = viewport_y + viewport_height * (1.0f - v.y) * 0.5f;
	}
#else
	// Map to 2D viewport:
	__m128 mviewport_x = _mm_set1_ps((float)viewport_x);
	__m128 mviewport_y = _mm_set1_ps((float)viewport_y);
	__m128 mviewport_halfwidth = _mm_set1_ps(viewport_width * 0.5f);
	__m128 mviewport_halfheight = _mm_set1_ps(viewport_height * 0.5f);
	__m128 mone = _mm_set1_ps(1.0f);
	int sse_length = (numclipvert + 3) / 4 * 4;
	for (int j = 0; j < sse_length; j += 4)
	{
		__m128 vx = _mm_loadu_ps(&clippedvert[j].x);
		__m128 vy = _mm_loadu_ps(&clippedvert[j + 1].x);
		__m128 vz = _mm_loadu_ps(&clippedvert[j + 2].x);
		__m128 vw = _mm_loadu_ps(&clippedvert[j + 3].x);
		_MM_TRANSPOSE4_PS(vx, vy, vz, vw);

		// Calculate normalized device coordinates:
		vw = _mm_div_ps(mone, vw);
		vx = _mm_mul_ps(vx, vw);
		vy = _mm_mul_ps(vy, vw);
		vz = _mm_mul_ps(vz, vw);

		// Apply viewport scale to get screen coordinates:
		vx = _mm_add_ps(mviewport_x, _mm_mul_ps(mviewport_halfwidth, _mm_add_ps(mone, vx)));
		vy = _mm_add_ps(mviewport_y, _mm_mul_ps(mviewport_halfheight, _mm_sub_ps(mone, vy)));

		_MM_TRANSPOSE4_PS(vx, vy, vz, vw);
		_mm_storeu_ps(&clippedvert[j].x, vx);
		_mm_storeu_ps(&clippedvert[j + 1].x, vy);
		_mm_storeu_ps(&clippedvert[j + 2].x, vz);
		_mm_storeu_ps(&clippedvert[j + 3].x, vw);
	}
#endif

	// Keep varyings in -128 to 128 range if possible
	// But don't do this for the skycap mode since the V texture coordinate is used for blending
	if (numclipvert > 0 && args->uniforms->BlendMode() != TriBlendMode::Skycap)
	{
		float newOriginU = floorf(clippedvert[0].u * 0.1f) * 10.0f;
		float newOriginV = floorf(clippedvert[0].v * 0.1f) * 10.0f;
		for (int i = 0; i < numclipvert; i++)
		{
			clippedvert[i].u -= newOriginU;
			clippedvert[i].v -= newOriginV;
		}
	}

	// Draw screen triangles
	if (ccw)
	{
		for (int i = numclipvert - 1; i > 1; i--)
		{
			args->v1 = &clippedvert[numclipvert - 1];
			args->v2 = &clippedvert[i - 1];
			args->v3 = &clippedvert[i - 2];
			if (args->CalculateGradients())
				ScreenTriangle::Draw(args, thread);
		}
	}
	else
	{
		for (int i = 2; i < numclipvert; i++)
		{
			args->v1 = &clippedvert[0];
			args->v2 = &clippedvert[i - 1];
			args->v3 = &clippedvert[i];
			if (args->CalculateGradients())
				ScreenTriangle::Draw(args, thread);
		}
	}
}

int PolyTriangleDrawer::clipedge(const ShadedTriVertex *verts, TriVertex *clippedvert)
{
	// Clip and cull so that the following is true for all vertices:
	// -v.w <= v.x <= v.w
	// -v.w <= v.y <= v.w
	// -v.w <= v.z <= v.w
	
	// halfspace clip distances
	static const int numclipdistances = 7;
#ifdef NO_SSE
	float clipdistance[numclipdistances * 3];
	bool needsclipping = false;
	float *clipd = clipdistance;
	for (int i = 0; i < 3; i++)
	{
		const auto &v = verts[i];
		clipd[0] = v.x + v.w;
		clipd[1] = v.w - v.x;
		clipd[2] = v.y + v.w;
		clipd[3] = v.w - v.y;
		clipd[4] = v.z + v.w;
		clipd[5] = v.w - v.z;
		clipd[6] = v.clipDistance0;
		needsclipping = needsclipping || clipd[0] < 0.0f || clipd[1] < 0.0f || clipd[2] < 0.0f || clipd[3] < 0.0f || clipd[4] < 0.0f || clipd[5] < 0.0f || clipd[6] < 0.0f;
		clipd += numclipdistances;
	}

	// If all halfspace clip distances are positive then the entire triangle is visible. Skip the expensive clipping step.
	if (!needsclipping)
	{
		for (int i = 0; i < 3; i++)
		{
			memcpy(clippedvert + i, verts + i, sizeof(TriVertex));
		}
		return 3;
	}
#else
	__m128 mx = _mm_loadu_ps(&verts[0].x);
	__m128 my = _mm_loadu_ps(&verts[1].x);
	__m128 mz = _mm_loadu_ps(&verts[2].x);
	__m128 mw = _mm_setzero_ps();
	_MM_TRANSPOSE4_PS(mx, my, mz, mw);
	__m128 clipd0 = _mm_add_ps(mx, mw);
	__m128 clipd1 = _mm_sub_ps(mw, mx);
	__m128 clipd2 = _mm_add_ps(my, mw);
	__m128 clipd3 = _mm_sub_ps(mw, my);
	__m128 clipd4 = _mm_add_ps(mz, mw);
	__m128 clipd5 = _mm_sub_ps(mw, mz);
	__m128 clipd6 = _mm_setr_ps(verts[0].clipDistance0, verts[1].clipDistance0, verts[2].clipDistance0, 0.0f);
	__m128 mneedsclipping = _mm_cmplt_ps(clipd0, _mm_setzero_ps());
	mneedsclipping = _mm_or_ps(mneedsclipping, _mm_cmplt_ps(clipd1, _mm_setzero_ps()));
	mneedsclipping = _mm_or_ps(mneedsclipping, _mm_cmplt_ps(clipd2, _mm_setzero_ps()));
	mneedsclipping = _mm_or_ps(mneedsclipping, _mm_cmplt_ps(clipd3, _mm_setzero_ps()));
	mneedsclipping = _mm_or_ps(mneedsclipping, _mm_cmplt_ps(clipd4, _mm_setzero_ps()));
	mneedsclipping = _mm_or_ps(mneedsclipping, _mm_cmplt_ps(clipd5, _mm_setzero_ps()));
	mneedsclipping = _mm_or_ps(mneedsclipping, _mm_cmplt_ps(clipd6, _mm_setzero_ps()));
	if (_mm_movemask_ps(mneedsclipping) == 0)
	{
		for (int i = 0; i < 3; i++)
		{
			memcpy(clippedvert + i, verts + i, sizeof(TriVertex));
		}
		return 3;
	}
	float clipdistance[numclipdistances * 4];
	_mm_storeu_ps(clipdistance, clipd0);
	_mm_storeu_ps(clipdistance + 4, clipd1);
	_mm_storeu_ps(clipdistance + 8, clipd2);
	_mm_storeu_ps(clipdistance + 12, clipd3);
	_mm_storeu_ps(clipdistance + 16, clipd4);
	_mm_storeu_ps(clipdistance + 20, clipd5);
	_mm_storeu_ps(clipdistance + 24, clipd6);
#endif

	// use barycentric weights while clipping vertices
	float weights[max_additional_vertices * 3 * 2];
	for (int i = 0; i < 3; i++)
	{
		weights[i * 3 + 0] = 0.0f;
		weights[i * 3 + 1] = 0.0f;
		weights[i * 3 + 2] = 0.0f;
		weights[i * 3 + i] = 1.0f;
	}

	// Clip against each halfspace
	float *input = weights;
	float *output = weights + max_additional_vertices * 3;
	int inputverts = 3;
	for (int p = 0; p < numclipdistances; p++)
	{
		// Clip each edge
		int outputverts = 0;
		for (int i = 0; i < inputverts; i++)
		{
			int j = (i + 1) % inputverts;
#ifdef NO_SSE
			float clipdistance1 =
				clipdistance[0 * numclipdistances + p] * input[i * 3 + 0] +
				clipdistance[1 * numclipdistances + p] * input[i * 3 + 1] +
				clipdistance[2 * numclipdistances + p] * input[i * 3 + 2];

			float clipdistance2 =
				clipdistance[0 * numclipdistances + p] * input[j * 3 + 0] +
				clipdistance[1 * numclipdistances + p] * input[j * 3 + 1] +
				clipdistance[2 * numclipdistances + p] * input[j * 3 + 2];
#else
			float clipdistance1 =
				clipdistance[0 + p * 4] * input[i * 3 + 0] +
				clipdistance[1 + p * 4] * input[i * 3 + 1] +
				clipdistance[2 + p * 4] * input[i * 3 + 2];

			float clipdistance2 =
				clipdistance[0 + p * 4] * input[j * 3 + 0] +
				clipdistance[1 + p * 4] * input[j * 3 + 1] +
				clipdistance[2 + p * 4] * input[j * 3 + 2];
#endif

			// Clip halfspace
			if ((clipdistance1 >= 0.0f || clipdistance2 >= 0.0f) && outputverts + 1 < max_additional_vertices)
			{
				float t1 = (clipdistance1 < 0.0f) ? MAX(-clipdistance1 / (clipdistance2 - clipdistance1), 0.0f) : 0.0f;
				float t2 = (clipdistance2 < 0.0f) ? MIN(1.0f + clipdistance2 / (clipdistance1 - clipdistance2), 1.0f) : 1.0f;

				// add t1 vertex
				for (int k = 0; k < 3; k++)
					output[outputverts * 3 + k] = input[i * 3 + k] * (1.0f - t1) + input[j * 3 + k] * t1;
				outputverts++;
			
				if (t2 != 1.0f && t2 > t1)
				{
					// add t2 vertex
					for (int k = 0; k < 3; k++)
						output[outputverts * 3 + k] = input[i * 3 + k] * (1.0f - t2) + input[j * 3 + k] * t2;
					outputverts++;
				}
			}
		}
		std::swap(input, output);
		inputverts = outputverts;
		if (inputverts == 0)
			break;
	}
	
	// Convert barycentric weights to actual vertices
	for (int i = 0; i < inputverts; i++)
	{
		auto &v = clippedvert[i];
		memset(&v, 0, sizeof(TriVertex));
		for (int w = 0; w < 3; w++)
		{
			float weight = input[i * 3 + w];
			v.x += verts[w].x * weight;
			v.y += verts[w].y * weight;
			v.z += verts[w].z * weight;
			v.w += verts[w].w * weight;
			v.u += verts[w].u * weight;
			v.v += verts[w].v * weight;
		}
	}
	return inputverts;
}

/////////////////////////////////////////////////////////////////////////////

DrawPolyTrianglesCommand::DrawPolyTrianglesCommand(const PolyDrawArgs &args, bool mirror)
	: args(args)
{
	if (mirror)
		this->args.SetFaceCullCCW(!this->args.FaceCullCCW());
}

void DrawPolyTrianglesCommand::Execute(DrawerThread *thread)
{
	WorkerThreadData thread_data;
	thread_data.core = thread->core;
	thread_data.num_cores = thread->num_cores;

	PolyTriangleDrawer::draw_arrays(args, &thread_data);
}

/////////////////////////////////////////////////////////////////////////////

void DrawRectCommand::Execute(DrawerThread *thread)
{
	WorkerThreadData thread_data;
	thread_data.core = thread->core;
	thread_data.num_cores = thread->num_cores;

	auto renderTarget = PolyRenderer::Instance()->RenderTarget;
	const void *destOrg = renderTarget->GetBuffer();
	int destWidth = renderTarget->GetWidth();
	int destHeight = renderTarget->GetHeight();
	int destPitch = renderTarget->GetPitch();
	int blendmode = (int)args.BlendMode();
	if (renderTarget->IsBgra())
		ScreenTriangle::RectDrawers32[blendmode](destOrg, destWidth, destHeight, destPitch, &args, &thread_data);
	else
		ScreenTriangle::RectDrawers8[blendmode](destOrg, destWidth, destHeight, destPitch, &args, &thread_data);
}

/////////////////////////////////////////////////////////////////////////////

void DeferredLightCommand::Execute(DrawerThread *thread)
{
	uint32_t *framebuffer = (uint32_t*)screen->GetBuffer();
	float *zbuffer = PolyZBuffer::Instance()->Values();

	float globVis = mGlobVis * (1.0f / 32.0f);

	int pitch = screen->GetPitch();
	int blockPitch = PolyStencilBuffer::Instance()->BlockWidth() * 64;

	int blockWidth = screen->GetWidth() / 8;
	int blockHeight = screen->GetHeight() / 8;
	for (int blockY = thread->core; blockY < blockHeight; blockY += thread->num_cores)
	{
		int y = blockY << 3;
		for (int blockX = 0; blockX < blockWidth; blockX++)
		{
			int x = blockX << 3;

			float *zblock = zbuffer + (blockX << 6) + blockY * blockPitch;
			uint32_t *line = framebuffer + y * pitch + x;

#ifdef NO_SSE
			for (int iy = 0; iy < 8; iy++)
			{
				for (int ix = 0; ix < 8; ix++)
				{
					uint32_t fg = line[ix];
					uint32_t red = RPART(fg);
					uint32_t green = GPART(fg);
					uint32_t blue = BPART(fg);

					float invz = zblock[ix];

					uint32_t light = APART(fg);
					if ((light & 1) == 0)
					{
						float shade = 2.0f - (light + 12.0f) / 128.0f;
						light = FRACUNIT - (fixed_t)(clamp(shade - MIN(24.0f / 32.0f, globVis * invz), 0.0f, 31.0f / 32.0f) * (float)FRACUNIT);
						light >>= 8;
					}
					else // fixed light
					{
						light += light >> 7;
					}

					red = (red * light + 127) >> 8;
					green = (green * light + 127) >> 8;
					blue = (blue * light + 127) >> 8;

					line[ix] = 0xff000000 | (red << 16) | (green << 8) | blue;
				}
				line += pitch;
				zblock += 8;
			}
#else
			__m128 mmglobVis = _mm_set1_ps(globVis);
			for (int iy = 0; iy < 8; iy++)
			{
				for (int ix = 0; ix < 8; ix += 4)
				{
					__m128i fg = _mm_loadu_si128((const __m128i*)(line + ix));
					__m128 invz = _mm_loadu_ps(zblock + ix);

					__m128i light = _mm_srli_epi32(fg, 24);
					__m128 shade = _mm_sub_ps(_mm_set1_ps(2.0f), _mm_mul_ps(_mm_add_ps(_mm_cvtepi32_ps(light), _mm_set1_ps(12.0f)), _mm_set1_ps(1.0f / 128.0f)));

					__m128i isfixedlightmask = _mm_cmpeq_epi32(_mm_and_si128(light, _mm_set1_epi32(1)), _mm_setzero_si128());
					__m128i fixedlight = _mm_add_epi32(light, _mm_srli_epi32(light, 7));
					light = _mm_sub_epi32(_mm_set1_epi32(FRACUNIT), _mm_cvtps_epi32(_mm_mul_ps(_mm_min_ps(_mm_max_ps(_mm_sub_ps(shade,_mm_min_ps(_mm_set1_ps(24.0f / 32.0f),_mm_mul_ps(mmglobVis, invz))),_mm_setzero_ps()),_mm_set1_ps(31.0f / 32.0f)), _mm_set1_ps((float)FRACUNIT))));
					light = _mm_srli_epi32(light, 8);
					light = _mm_or_si128(_mm_and_si128(isfixedlightmask, light), _mm_andnot_si128(isfixedlightmask, fixedlight));

					__m128i fglo = _mm_unpacklo_epi8(fg, _mm_setzero_si128());
					__m128i fghi = _mm_unpackhi_epi8(fg, _mm_setzero_si128());
					__m128i m127 = _mm_set1_epi16(127);
					__m128i lightlo = _mm_packs_epi32(_mm_shuffle_epi32(light, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_epi32(light, _MM_SHUFFLE(1, 1, 1, 1)));
					__m128i lighthi = _mm_packs_epi32(_mm_shuffle_epi32(light, _MM_SHUFFLE(2, 2, 2, 2)), _mm_shuffle_epi32(light, _MM_SHUFFLE(3, 3, 3, 3)));
					fglo = _mm_srli_epi16(_mm_add_epi16(_mm_mullo_epi16(fglo, lightlo), m127), 8);
					fghi = _mm_srli_epi16(_mm_add_epi16(_mm_mullo_epi16(fghi, lighthi), m127), 8);
					fg = _mm_packus_epi16(fglo, fghi);

					_mm_storeu_si128((__m128i*)(line + ix), fg);
				}
				line += pitch;
				zblock += 8;
			}
#endif
		}
	}
}
