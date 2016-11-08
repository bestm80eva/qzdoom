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
#include "r_local.h"
#include "v_video.h"
#include "doomstat.h"
#include "st_stuff.h"
#include "g_game.h"
#include "g_level.h"
#include "r_data/r_translate.h"
#include "v_palette.h"
#include "r_data/colormaps.h"
#include "r_triangle.h"

void TriangleDrawer::draw(const TriUniforms &uniforms, const TriVertex *vinput, int vcount, TriangleDrawMode mode, bool ccw, int clipleft, int clipright, const short *cliptop, const short *clipbottom, FTexture *texture)
{
	if (r_swtruecolor)
		queue_arrays(uniforms, vinput, vcount, mode, ccw, clipleft, clipright, cliptop, clipbottom, (const uint8_t*)texture->GetPixelsBgra(), texture->GetWidth(), texture->GetHeight(), 0);
	else
		draw_arrays(uniforms, vinput, vcount, mode, ccw, clipleft, clipright, cliptop, clipbottom, texture->GetPixels(), texture->GetWidth(), texture->GetHeight(), 0, nullptr, &ScreenTriangleDrawer::draw);
}

void TriangleDrawer::fill(const TriUniforms &uniforms, const TriVertex *vinput, int vcount, TriangleDrawMode mode, bool ccw, int clipleft, int clipright, const short *cliptop, const short *clipbottom, int solidcolor)
{
	if (r_swtruecolor)
		queue_arrays(uniforms, vinput, vcount, mode, ccw, clipleft, clipright, cliptop, clipbottom, nullptr, 0, 0, solidcolor);
	else
		draw_arrays(uniforms, vinput, vcount, mode, ccw, clipleft, clipright, cliptop, clipbottom, nullptr, 0, 0, solidcolor, nullptr, &ScreenTriangleDrawer::fill);
}

void TriangleDrawer::queue_arrays(const TriUniforms &uniforms, const TriVertex *vinput, int vcount, TriangleDrawMode mode, bool ccw, int clipleft, int clipright, const short *cliptop, const short *clipbottom, const uint8_t *texturePixels, int textureWidth, int textureHeight, int solidcolor)
{
	if (clipright < clipleft || clipleft < 0 || clipright > MAXWIDTH)
		return;

	int cliplength = clipright - clipleft + 1;
	short *clipdata = (short*)DrawerCommandQueue::AllocMemory(cliplength * 2 * sizeof(short));
	if (!clipdata)
	{
		DrawerCommandQueue::WaitForWorkers();
		clipdata = (short*)DrawerCommandQueue::AllocMemory(cliplength * 2 * sizeof(short));
		if (!clipdata)
			return;
	}

	for (int i = 0; i < cliplength; i++)
		clipdata[i] = cliptop[clipleft + i];
	for (int i = 0; i < cliplength; i++)
		clipdata[cliplength + i] = clipbottom[clipleft + i];

	DrawerCommandQueue::QueueCommand<DrawTrianglesCommand>(uniforms, vinput, vcount, mode, ccw, clipleft, clipright, clipdata, texturePixels, textureWidth, textureHeight, solidcolor);
}

void TriangleDrawer::draw_arrays(const TriUniforms &uniforms, const TriVertex *vinput, int vcount, TriangleDrawMode mode, bool ccw, int clipleft, int clipright, const short *cliptop, const short *clipbottom, const uint8_t *texturePixels, int textureWidth, int textureHeight, int solidcolor, DrawerThread *thread, void(*drawfunc)(const ScreenTriangleDrawerArgs *, DrawerThread *))
{
	if (vcount < 3)
		return;

	ScreenTriangleDrawerArgs args;
	args.dest = dc_destorg;
	args.pitch = dc_pitch;
	args.clipleft = clipleft;
	args.clipright = clipright;
	args.cliptop = cliptop;
	args.clipbottom = clipbottom;
	args.texturePixels = texturePixels;
	args.textureWidth = textureWidth;
	args.textureHeight = textureHeight;
	args.solidcolor = solidcolor;
	args.uniforms = &uniforms;

	TriVertex vert[3];
	if (mode == TriangleDrawMode::Normal)
	{
		for (int i = 0; i < vcount / 3; i++)
		{
			for (int j = 0; j < 3; j++)
				vert[j] = shade_vertex(uniforms, *(vinput++));
			draw_shaded_triangle(vert, ccw, &args, thread, drawfunc);
		}
	}
	else if (mode == TriangleDrawMode::Fan)
	{
		vert[0] = shade_vertex(uniforms, *(vinput++));
		vert[1] = shade_vertex(uniforms, *(vinput++));
		for (int i = 2; i < vcount; i++)
		{
			vert[2] = shade_vertex(uniforms, *(vinput++));
			draw_shaded_triangle(vert, ccw, &args, thread, drawfunc);
			vert[1] = vert[2];
		}
	}
	else // TriangleDrawMode::Strip
	{
		vert[0] = shade_vertex(uniforms, *(vinput++));
		vert[1] = shade_vertex(uniforms, *(vinput++));
		for (int i = 2; i < vcount; i++)
		{
			vert[2] = shade_vertex(uniforms, *(vinput++));
			draw_shaded_triangle(vert, ccw, &args, thread, drawfunc);
			vert[0] = vert[1];
			vert[1] = vert[2];
			ccw = !ccw;
		}
	}
}

TriVertex TriangleDrawer::shade_vertex(const TriUniforms &uniforms, TriVertex v)
{
	// Apply transform to get clip coordinates:
	return uniforms.objectToClip * v;
}

void TriangleDrawer::draw_shaded_triangle(const TriVertex *vert, bool ccw, ScreenTriangleDrawerArgs *args, DrawerThread *thread, void(*drawfunc)(const ScreenTriangleDrawerArgs *, DrawerThread *))
{
	// Cull, clip and generate additional vertices as needed
	TriVertex clippedvert[6];
	int numclipvert = 0;
	clipedge(vert[0], vert[1], clippedvert, numclipvert);
	clipedge(vert[1], vert[2], clippedvert, numclipvert);
	clipedge(vert[2], vert[0], clippedvert, numclipvert);

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
		v.x = viewwidth * (1.0f + v.x) * 0.5f;
		v.y = viewheight * (1.0f - v.y) * 0.5f;
	}

	// Draw screen triangles
	if (ccw)
	{
		for (int i = numclipvert; i > 1; i--)
		{
			args->v1 = &clippedvert[numclipvert - 1];
			args->v2 = &clippedvert[i - 1];
			args->v3 = &clippedvert[i - 2];
			drawfunc(args, thread);
		}
	}
	else
	{
		for (int i = 2; i < numclipvert; i++)
		{
			args->v1 = &clippedvert[0];
			args->v2 = &clippedvert[i - 1];
			args->v3 = &clippedvert[i];
			drawfunc(args, thread);
		}
	}
}

bool TriangleDrawer::cullhalfspace(float clipdistance1, float clipdistance2, float &t1, float &t2)
{
	float d1 = clipdistance1 * (1.0f - t1) + clipdistance2 * t1;
	float d2 = clipdistance1 * (1.0f - t2) + clipdistance2 * t2;
	if (d1 < 0.0f && d2 < 0.0f)
		return true;

	if (d1 < 0.0f)
		t1 = MAX(-clipdistance1 / (clipdistance2 - clipdistance1), t1);

	if (d2 < 0.0f)
		t2 = MIN(1.0f + clipdistance2 / (clipdistance1 - clipdistance2), t2);

	return false;
}

void TriangleDrawer::clipedge(const TriVertex &v1, const TriVertex &v2, TriVertex *clippedvert, int &numclipvert)
{
	// Clip and cull so that the following is true for all vertices:
	// -v.w <= v.x <= v.w
	// -v.w <= v.y <= v.w
	// -v.w <= v.z <= v.w

	float t1 = 0.0f, t2 = 1.0f;
	bool culled =
		cullhalfspace(v1.x + v1.w, v2.x + v2.w, t1, t2) ||
		cullhalfspace(v1.w - v1.x, v2.w - v2.x, t1, t2) ||
		cullhalfspace(v1.y + v1.w, v2.y + v2.w, t1, t2) ||
		cullhalfspace(v1.w - v1.y, v2.w - v2.y, t1, t2) ||
		cullhalfspace(v1.z + v1.w, v2.z + v2.w, t1, t2) ||
		cullhalfspace(v1.w - v1.z, v2.w - v2.z, t1, t2);
	if (culled)
		return;

	if (t1 == 0.0f)
	{
		clippedvert[numclipvert++] = v1;
	}
	else
	{
		auto &v = clippedvert[numclipvert++];
		v.x = v1.x * (1.0f - t1) + v2.x * t1;
		v.y = v1.y * (1.0f - t1) + v2.y * t1;
		v.z = v1.z * (1.0f - t1) + v2.z * t1;
		v.w = v1.w * (1.0f - t1) + v2.w * t1;
		for (int i = 0; i < TriVertex::NumVarying; i++)
			v.varying[i] = v1.varying[i] * (1.0f - t1) + v2.varying[i] * t1;
	}

	if (t2 != 1.0f)
	{
		auto &v = clippedvert[numclipvert++];
		v.x = v1.x * (1.0f - t2) + v2.x * t2;
		v.y = v1.y * (1.0f - t2) + v2.y * t2;
		v.z = v1.z * (1.0f - t2) + v2.z * t2;
		v.w = v1.w * (1.0f - t2) + v2.w * t2;
		for (int i = 0; i < TriVertex::NumVarying; i++)
			v.varying[i] = v1.varying[i] * (1.0f - t2) + v2.varying[i] * t2;
	}
}

/////////////////////////////////////////////////////////////////////////////

void ScreenTriangleDrawer::draw(const ScreenTriangleDrawerArgs *args, DrawerThread *thread)
{
	uint8_t *dest = args->dest;
	int pitch = args->pitch;
	const TriVertex &v1 = *args->v1;
	const TriVertex &v2 = *args->v2;
	const TriVertex &v3 = *args->v3;
	int clipleft = args->clipleft;
	int clipright = args->clipright;
	const short *cliptop = args->cliptop;
	const short *clipbottom = args->clipbottom;
	const uint8_t *texturePixels = args->texturePixels;
	int textureWidth = args->textureWidth;
	int textureHeight = args->textureHeight;

	// 28.4 fixed-point coordinates
	const int Y1 = (int)round(16.0f * v1.y);
	const int Y2 = (int)round(16.0f * v2.y);
	const int Y3 = (int)round(16.0f * v3.y);

	const int X1 = (int)round(16.0f * v1.x);
	const int X2 = (int)round(16.0f * v2.x);
	const int X3 = (int)round(16.0f * v3.x);

	// Deltas
	const int DX12 = X1 - X2;
	const int DX23 = X2 - X3;
	const int DX31 = X3 - X1;

	const int DY12 = Y1 - Y2;
	const int DY23 = Y2 - Y3;
	const int DY31 = Y3 - Y1;

	// Fixed-point deltas
	const int FDX12 = DX12 << 4;
	const int FDX23 = DX23 << 4;
	const int FDX31 = DX31 << 4;

	const int FDY12 = DY12 << 4;
	const int FDY23 = DY23 << 4;
	const int FDY31 = DY31 << 4;

	// Bounding rectangle
	int clipymin = cliptop[clipleft];
	int clipymax = clipbottom[clipleft];
	for (int i = clipleft + 1; i <= clipright; i++)
	{
		clipymin = MIN(clipymin, (int)cliptop[i]);
		clipymax = MAX(clipymax, (int)clipbottom[i]);
	}
	int minx = MAX((MIN(MIN(X1, X2), X3) + 0xF) >> 4, clipleft);
	int maxx = MIN((MAX(MAX(X1, X2), X3) + 0xF) >> 4, clipright);
	int miny = MAX((MIN(MIN(Y1, Y2), Y3) + 0xF) >> 4, clipymin);
	int maxy = MIN((MAX(MAX(Y1, Y2), Y3) + 0xF) >> 4, clipymax - 1);
	if (minx >= maxx || miny >= maxy)
		return;

	// Block size, standard 8x8 (must be power of two)
	const int q = 8;

	// Start in corner of 8x8 block
	minx &= ~(q - 1);
	miny &= ~(q - 1);

	dest += miny * pitch;

	// Half-edge constants
	int C1 = DY12 * X1 - DX12 * Y1;
	int C2 = DY23 * X2 - DX23 * Y2;
	int C3 = DY31 * X3 - DX31 * Y3;

	// Correct for fill convention
	if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
	if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
	if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

	// Gradients
	float gradWX = gradx(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.w, v2.w, v3.w);
	float gradWY = grady(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.w, v2.w, v3.w);
	float startW = v1.w + gradWX * (minx - v1.x) + gradWY * (miny - v1.y);
	float gradVaryingX[TriVertex::NumVarying], gradVaryingY[TriVertex::NumVarying], startVarying[TriVertex::NumVarying];
	for (int i = 0; i < TriVertex::NumVarying; i++)
	{
		gradVaryingX[i] = gradx(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.varying[i] * v1.w, v2.varying[i] * v2.w, v3.varying[i] * v3.w);
		gradVaryingY[i] = grady(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.varying[i] * v1.w, v2.varying[i] * v2.w, v3.varying[i] * v3.w);
		startVarying[i] = v1.varying[i] * v1.w + gradVaryingX[i] * (minx - v1.x) + gradVaryingY[i] * (miny - v1.y);
	}

	// Loop through blocks
	for (int y = miny; y < maxy; y += q)
	{
		for (int x = minx; x < maxx; x += q)
		{
			// Corners of block
			int x0 = x << 4;
			int x1 = (x + q - 1) << 4;
			int y0 = y << 4;
			int y1 = (y + q - 1) << 4;

			// Evaluate half-space functions
			bool a00 = C1 + DX12 * y0 - DY12 * x0 > 0;
			bool a10 = C1 + DX12 * y0 - DY12 * x1 > 0;
			bool a01 = C1 + DX12 * y1 - DY12 * x0 > 0;
			bool a11 = C1 + DX12 * y1 - DY12 * x1 > 0;
			int a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);

			bool b00 = C2 + DX23 * y0 - DY23 * x0 > 0;
			bool b10 = C2 + DX23 * y0 - DY23 * x1 > 0;
			bool b01 = C2 + DX23 * y1 - DY23 * x0 > 0;
			bool b11 = C2 + DX23 * y1 - DY23 * x1 > 0;
			int b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);

			bool c00 = C3 + DX31 * y0 - DY31 * x0 > 0;
			bool c10 = C3 + DX31 * y0 - DY31 * x1 > 0;
			bool c01 = C3 + DX31 * y1 - DY31 * x0 > 0;
			bool c11 = C3 + DX31 * y1 - DY31 * x1 > 0;
			int c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);

			// Skip block when outside an edge
			if (a == 0x0 || b == 0x0 || c == 0x0) continue;

			// Check if block needs clipping
			int clipcount = 0;
			for (int ix = x; ix < x + q; ix++)
			{
				clipcount += (clipleft > ix) || (clipright < ix) || (cliptop[ix] > y) || (clipbottom[ix] <= y + q - 1);
			}

			// Calculate varying variables for affine block
			float offx0 = (x - minx) + 0.5f;
			float offy0 = (y - miny) + 0.5f;
			float offx1 = offx0 + q;
			float offy1 = offy0 + q;
			float rcpWTL = 1.0f / (startW + offx0 * gradWX + offy0 * gradWY);
			float rcpWTR = 1.0f / (startW + offx1 * gradWX + offy0 * gradWY);
			float rcpWBL = 1.0f / (startW + offx0 * gradWX + offy1 * gradWY);
			float rcpWBR = 1.0f / (startW + offx1 * gradWX + offy1 * gradWY);
			float varyingTL[TriVertex::NumVarying];
			float varyingTR[TriVertex::NumVarying];
			float varyingBL[TriVertex::NumVarying];
			float varyingBR[TriVertex::NumVarying];
			for (int i = 0; i < TriVertex::NumVarying; i++)
			{
				varyingTL[i] = (startVarying[i] + offx0 * gradVaryingX[i] + offy0 * gradVaryingY[i]) * rcpWTL;
				varyingTR[i] = (startVarying[i] + offx1 * gradVaryingX[i] + offy0 * gradVaryingY[i]) * rcpWTR;
				varyingBL[i] = ((startVarying[i] + offx0 * gradVaryingX[i] + offy1 * gradVaryingY[i]) * rcpWBL - varyingTL[i]) * (1.0f / q);
				varyingBR[i] = ((startVarying[i] + offx1 * gradVaryingX[i] + offy1 * gradVaryingY[i]) * rcpWBR - varyingTR[i]) * (1.0f / q);
			}

			uint8_t *buffer = dest;

			// Accept whole block when totally covered
			if (a == 0xF && b == 0xF && c == 0xF && clipcount == 0)
			{
				for (int iy = 0; iy < q; iy++)
				{
					uint32_t varying[TriVertex::NumVarying], varyingStep[TriVertex::NumVarying];
					for (int i = 0; i < TriVertex::NumVarying; i++)
					{
						float pos = varyingTL[i] + varyingBL[i] * iy;
						float step = (varyingTR[i] + varyingBR[i] * iy - pos) * (1.0f / q);

						varying[i] = (uint32_t)((pos - floor(pos)) * 0x100000000LL);
						varyingStep[i] = (uint32_t)(step * 0x100000000LL);
					}

					for (int ix = x; ix < x + q; ix++)
					{
						uint32_t ufrac = varying[0];
						uint32_t vfrac = varying[1];

						uint32_t upos = ((ufrac >> 16) * textureWidth) >> 16;
						uint32_t vpos = ((vfrac >> 16) * textureHeight) >> 16;
						uint32_t uvoffset = upos * textureHeight + vpos;

						buffer[ix] = texturePixels[uvoffset];

						for (int i = 0; i < TriVertex::NumVarying; i++)
							varying[i] += varyingStep[i];
					}

					buffer += pitch;
				}
			}
			else // Partially covered block
			{
				int CY1 = C1 + DX12 * y0 - DY12 * x0;
				int CY2 = C2 + DX23 * y0 - DY23 * x0;
				int CY3 = C3 + DX31 * y0 - DY31 * x0;

				for (int iy = 0; iy < q; iy++)
				{
					int CX1 = CY1;
					int CX2 = CY2;
					int CX3 = CY3;

					uint32_t varying[TriVertex::NumVarying], varyingStep[TriVertex::NumVarying];
					for (int i = 0; i < TriVertex::NumVarying; i++)
					{
						float pos = varyingTL[i] + varyingBL[i] * iy;
						float step = (varyingTR[i] + varyingBR[i] * iy - pos) * (1.0f / q);

						varying[i] = (uint32_t)((pos - floor(pos)) * 0x100000000LL);
						varyingStep[i] = (uint32_t)(step * 0x100000000LL);
					}

					for (int ix = x; ix < x + q; ix++)
					{
						bool visible = ix >= clipleft && ix <= clipright && (cliptop[ix] <= y + iy) && (clipbottom[ix] > y + iy);

						if (CX1 > 0 && CX2 > 0 && CX3 > 0 && visible)
						{
							uint32_t ufrac = varying[0];
							uint32_t vfrac = varying[1];

							uint32_t upos = ((ufrac >> 16) * textureWidth) >> 16;
							uint32_t vpos = ((vfrac >> 16) * textureHeight) >> 16;
							uint32_t uvoffset = upos * textureHeight + vpos;

							buffer[ix] = texturePixels[uvoffset];
						}

						for (int i = 0; i < TriVertex::NumVarying; i++)
							varying[i] += varyingStep[i];

						CX1 -= FDY12;
						CX2 -= FDY23;
						CX3 -= FDY31;
					}

					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;

					buffer += pitch;
				}
			}
		}

		dest += q * pitch;
	}
}

void ScreenTriangleDrawer::fill(const ScreenTriangleDrawerArgs *args, DrawerThread *thread)
{
	uint8_t *dest = args->dest;
	int pitch = args->pitch;
	const TriVertex &v1 = *args->v1;
	const TriVertex &v2 = *args->v2;
	const TriVertex &v3 = *args->v3;
	int clipleft = args->clipleft;
	int clipright = args->clipright;
	const short *cliptop = args->cliptop;
	const short *clipbottom = args->clipbottom;
	int solidcolor = args->solidcolor;

	// 28.4 fixed-point coordinates
	const int Y1 = (int)round(16.0f * v1.y);
	const int Y2 = (int)round(16.0f * v2.y);
	const int Y3 = (int)round(16.0f * v3.y);

	const int X1 = (int)round(16.0f * v1.x);
	const int X2 = (int)round(16.0f * v2.x);
	const int X3 = (int)round(16.0f * v3.x);

	// Deltas
	const int DX12 = X1 - X2;
	const int DX23 = X2 - X3;
	const int DX31 = X3 - X1;

	const int DY12 = Y1 - Y2;
	const int DY23 = Y2 - Y3;
	const int DY31 = Y3 - Y1;

	// Fixed-point deltas
	const int FDX12 = DX12 << 4;
	const int FDX23 = DX23 << 4;
	const int FDX31 = DX31 << 4;

	const int FDY12 = DY12 << 4;
	const int FDY23 = DY23 << 4;
	const int FDY31 = DY31 << 4;

	// Bounding rectangle
	int clipymin = cliptop[clipleft];
	int clipymax = clipbottom[clipleft];
	for (int i = clipleft + 1; i <= clipright; i++)
	{
		clipymin = MIN(clipymin, (int)cliptop[i]);
		clipymax = MAX(clipymax, (int)clipbottom[i]);
	}
	int minx = MAX((MIN(MIN(X1, X2), X3) + 0xF) >> 4, clipleft);
	int maxx = MIN((MAX(MAX(X1, X2), X3) + 0xF) >> 4, clipright);
	int miny = MAX((MIN(MIN(Y1, Y2), Y3) + 0xF) >> 4, clipymin);
	int maxy = MIN((MAX(MAX(Y1, Y2), Y3) + 0xF) >> 4, clipymax - 1);
	if (minx >= maxx || miny >= maxy)
		return;

	// Block size, standard 8x8 (must be power of two)
	const int q = 8;

	// Start in corner of 8x8 block
	minx &= ~(q - 1);
	miny &= ~(q - 1);

	dest += miny * pitch;

	// Half-edge constants
	int C1 = DY12 * X1 - DX12 * Y1;
	int C2 = DY23 * X2 - DX23 * Y2;
	int C3 = DY31 * X3 - DX31 * Y3;

	// Correct for fill convention
	if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
	if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
	if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

	// Loop through blocks
	for (int y = miny; y < maxy; y += q)
	{
		for (int x = minx; x < maxx; x += q)
		{
			// Corners of block
			int x0 = x << 4;
			int x1 = (x + q - 1) << 4;
			int y0 = y << 4;
			int y1 = (y + q - 1) << 4;

			// Evaluate half-space functions
			bool a00 = C1 + DX12 * y0 - DY12 * x0 > 0;
			bool a10 = C1 + DX12 * y0 - DY12 * x1 > 0;
			bool a01 = C1 + DX12 * y1 - DY12 * x0 > 0;
			bool a11 = C1 + DX12 * y1 - DY12 * x1 > 0;
			int a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);

			bool b00 = C2 + DX23 * y0 - DY23 * x0 > 0;
			bool b10 = C2 + DX23 * y0 - DY23 * x1 > 0;
			bool b01 = C2 + DX23 * y1 - DY23 * x0 > 0;
			bool b11 = C2 + DX23 * y1 - DY23 * x1 > 0;
			int b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);

			bool c00 = C3 + DX31 * y0 - DY31 * x0 > 0;
			bool c10 = C3 + DX31 * y0 - DY31 * x1 > 0;
			bool c01 = C3 + DX31 * y1 - DY31 * x0 > 0;
			bool c11 = C3 + DX31 * y1 - DY31 * x1 > 0;
			int c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);

			// Skip block when outside an edge
			if (a == 0x0 || b == 0x0 || c == 0x0) continue;

			// Check if block needs clipping
			int clipcount = 0;
			for (int ix = x; ix < x + q; ix++)
			{
				clipcount += (clipleft > ix) || (clipright < ix) || (cliptop[ix] > y) || (clipbottom[ix] <= y + q - 1);
			}

			uint8_t *buffer = dest;

			// Accept whole block when totally covered
			if (a == 0xF && b == 0xF && c == 0xF && clipcount == 0)
			{
				for (int iy = 0; iy < q; iy++)
				{
					for (int ix = x; ix < x + q; ix++)
					{
						buffer[ix] = solidcolor;
					}

					buffer += pitch;
				}
			}
			else // Partially covered block
			{
				int CY1 = C1 + DX12 * y0 - DY12 * x0;
				int CY2 = C2 + DX23 * y0 - DY23 * x0;
				int CY3 = C3 + DX31 * y0 - DY31 * x0;

				for (int iy = 0; iy < q; iy++)
				{
					int CX1 = CY1;
					int CX2 = CY2;
					int CX3 = CY3;

					for (int ix = x; ix < x + q; ix++)
					{
						bool visible = ix >= clipleft && ix <= clipright && (cliptop[ix] <= y + iy) && (clipbottom[ix] > y + iy);

						if (CX1 > 0 && CX2 > 0 && CX3 > 0 && visible)
						{
							buffer[ix] = solidcolor;
						}

						CX1 -= FDY12;
						CX2 -= FDY23;
						CX3 -= FDY31;
					}

					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;

					buffer += pitch;
				}
			}
		}

		dest += q * pitch;
	}
}

void ScreenTriangleDrawer::draw32(const ScreenTriangleDrawerArgs *args, DrawerThread *thread)
{
	uint32_t *dest = (uint32_t *)args->dest;
	int pitch = args->pitch;
	const TriVertex &v1 = *args->v1;
	const TriVertex &v2 = *args->v2;
	const TriVertex &v3 = *args->v3;
	int clipleft = args->clipleft;
	int clipright = args->clipright;
	const short *cliptop = args->cliptop;
	const short *clipbottom = args->clipbottom;
	const uint32_t *texturePixels = (const uint32_t *)args->texturePixels;
	int textureWidth = args->textureWidth;
	int textureHeight = args->textureHeight;
	uint32_t light = args->uniforms->light;

	// 28.4 fixed-point coordinates
	const int Y1 = (int)round(16.0f * v1.y);
	const int Y2 = (int)round(16.0f * v2.y);
	const int Y3 = (int)round(16.0f * v3.y);

	const int X1 = (int)round(16.0f * v1.x);
	const int X2 = (int)round(16.0f * v2.x);
	const int X3 = (int)round(16.0f * v3.x);

	// Deltas
	const int DX12 = X1 - X2;
	const int DX23 = X2 - X3;
	const int DX31 = X3 - X1;

	const int DY12 = Y1 - Y2;
	const int DY23 = Y2 - Y3;
	const int DY31 = Y3 - Y1;

	// Fixed-point deltas
	const int FDX12 = DX12 << 4;
	const int FDX23 = DX23 << 4;
	const int FDX31 = DX31 << 4;

	const int FDY12 = DY12 << 4;
	const int FDY23 = DY23 << 4;
	const int FDY31 = DY31 << 4;

	// Bounding rectangle
	int clipymin = cliptop[clipleft];
	int clipymax = clipbottom[clipleft];
	for (int i = clipleft + 1; i <= clipright; i++)
	{
		clipymin = MIN(clipymin, (int)cliptop[i]);
		clipymax = MAX(clipymax, (int)clipbottom[i]);
	}
	int minx = MAX((MIN(MIN(X1, X2), X3) + 0xF) >> 4, clipleft);
	int maxx = MIN((MAX(MAX(X1, X2), X3) + 0xF) >> 4, clipright);
	int miny = MAX((MIN(MIN(Y1, Y2), Y3) + 0xF) >> 4, clipymin);
	int maxy = MIN((MAX(MAX(Y1, Y2), Y3) + 0xF) >> 4, clipymax - 1);
	if (minx >= maxx || miny >= maxy)
		return;

	// Block size, standard 8x8 (must be power of two)
	const int q = 8;

	// Start in corner of 8x8 block
	minx &= ~(q - 1);
	miny &= ~(q - 1);

	dest += miny * pitch;

	// Half-edge constants
	int C1 = DY12 * X1 - DX12 * Y1;
	int C2 = DY23 * X2 - DX23 * Y2;
	int C3 = DY31 * X3 - DX31 * Y3;

	// Correct for fill convention
	if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
	if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
	if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

	// Gradients
	float gradWX = gradx(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.w, v2.w, v3.w);
	float gradWY = grady(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.w, v2.w, v3.w);
	float startW = v1.w + gradWX * (minx - v1.x) + gradWY * (miny - v1.y);
	float gradVaryingX[TriVertex::NumVarying], gradVaryingY[TriVertex::NumVarying], startVarying[TriVertex::NumVarying];
	for (int i = 0; i < TriVertex::NumVarying; i++)
	{
		gradVaryingX[i] = gradx(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.varying[i] * v1.w, v2.varying[i] * v2.w, v3.varying[i] * v3.w);
		gradVaryingY[i] = grady(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v1.varying[i] * v1.w, v2.varying[i] * v2.w, v3.varying[i] * v3.w);
		startVarying[i] = v1.varying[i] * v1.w + gradVaryingX[i] * (minx - v1.x) + gradVaryingY[i] * (miny - v1.y);
	}

	// Loop through blocks
	for (int y = miny; y < maxy; y += q)
	{
		for (int x = minx; x < maxx; x += q)
		{
			// Corners of block
			int x0 = x << 4;
			int x1 = (x + q - 1) << 4;
			int y0 = y << 4;
			int y1 = (y + q - 1) << 4;

			// Evaluate half-space functions
			bool a00 = C1 + DX12 * y0 - DY12 * x0 > 0;
			bool a10 = C1 + DX12 * y0 - DY12 * x1 > 0;
			bool a01 = C1 + DX12 * y1 - DY12 * x0 > 0;
			bool a11 = C1 + DX12 * y1 - DY12 * x1 > 0;
			int a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);

			bool b00 = C2 + DX23 * y0 - DY23 * x0 > 0;
			bool b10 = C2 + DX23 * y0 - DY23 * x1 > 0;
			bool b01 = C2 + DX23 * y1 - DY23 * x0 > 0;
			bool b11 = C2 + DX23 * y1 - DY23 * x1 > 0;
			int b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);

			bool c00 = C3 + DX31 * y0 - DY31 * x0 > 0;
			bool c10 = C3 + DX31 * y0 - DY31 * x1 > 0;
			bool c01 = C3 + DX31 * y1 - DY31 * x0 > 0;
			bool c11 = C3 + DX31 * y1 - DY31 * x1 > 0;
			int c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);

			// Skip block when outside an edge
			if (a == 0x0 || b == 0x0 || c == 0x0) continue;

			// Check if block needs clipping
			int clipcount = 0;
			for (int ix = x; ix < x + q; ix++)
			{
				clipcount += (clipleft > ix) || (clipright < ix) || (cliptop[ix] > y) || (clipbottom[ix] <= y + q - 1);
			}

			// Calculate varying variables for affine block
			float offx0 = (x - minx) + 0.5f;
			float offy0 = (y - miny) + 0.5f;
			float offx1 = offx0 + q;
			float offy1 = offy0 + q;
			float rcpWTL = 1.0f / (startW + offx0 * gradWX + offy0 * gradWY);
			float rcpWTR = 1.0f / (startW + offx1 * gradWX + offy0 * gradWY);
			float rcpWBL = 1.0f / (startW + offx0 * gradWX + offy1 * gradWY);
			float rcpWBR = 1.0f / (startW + offx1 * gradWX + offy1 * gradWY);
			float varyingTL[TriVertex::NumVarying];
			float varyingTR[TriVertex::NumVarying];
			float varyingBL[TriVertex::NumVarying];
			float varyingBR[TriVertex::NumVarying];
			for (int i = 0; i < TriVertex::NumVarying; i++)
			{
				varyingTL[i] = (startVarying[i] + offx0 * gradVaryingX[i] + offy0 * gradVaryingY[i]) * rcpWTL;
				varyingTR[i] = (startVarying[i] + offx1 * gradVaryingX[i] + offy0 * gradVaryingY[i]) * rcpWTR;
				varyingBL[i] = ((startVarying[i] + offx0 * gradVaryingX[i] + offy1 * gradVaryingY[i]) * rcpWBL - varyingTL[i]) * (1.0f / q);
				varyingBR[i] = ((startVarying[i] + offx1 * gradVaryingX[i] + offy1 * gradVaryingY[i]) * rcpWBR - varyingTR[i]) * (1.0f / q);
			}

			uint32_t *buffer = dest;

			// Accept whole block when totally covered
			if (a == 0xF && b == 0xF && c == 0xF && clipcount == 0)
			{
				for (int iy = 0; iy < q; iy++)
				{
					uint32_t varying[TriVertex::NumVarying], varyingStep[TriVertex::NumVarying];
					for (int i = 0; i < TriVertex::NumVarying; i++)
					{
						float pos = varyingTL[i] + varyingBL[i] * iy;
						float step = (varyingTR[i] + varyingBR[i] * iy - pos) * (1.0f / q);

						varying[i] = (uint32_t)((pos - floor(pos)) * 0x100000000LL);
						varyingStep[i] = (uint32_t)(step * 0x100000000LL);
					}

					if (!thread->skipped_by_thread(y + iy))
					{
						for (int ix = x; ix < x + q; ix++)
						{
							uint32_t ufrac = varying[0];
							uint32_t vfrac = varying[1];

							uint32_t upos = ((ufrac >> 16) * textureWidth) >> 16;
							uint32_t vpos = ((vfrac >> 16) * textureHeight) >> 16;
							uint32_t uvoffset = upos * textureHeight + vpos;

							uint32_t fg = texturePixels[uvoffset];
							uint32_t fg_red = (RPART(fg) * light) >> 8;
							uint32_t fg_green = (GPART(fg) * light) >> 8;
							uint32_t fg_blue = (BPART(fg) * light) >> 8;
							uint32_t fg_alpha = APART(fg);

							if (fg_alpha > 127)
								buffer[ix] = 0xff000000 | (fg_red << 16) | (fg_green << 8) | fg_blue;

							for (int i = 0; i < TriVertex::NumVarying; i++)
								varying[i] += varyingStep[i];
						}
					}

					buffer += pitch;
				}
			}
			else // Partially covered block
			{
				int CY1 = C1 + DX12 * y0 - DY12 * x0;
				int CY2 = C2 + DX23 * y0 - DY23 * x0;
				int CY3 = C3 + DX31 * y0 - DY31 * x0;

				for (int iy = 0; iy < q; iy++)
				{
					int CX1 = CY1;
					int CX2 = CY2;
					int CX3 = CY3;

					float varying[TriVertex::NumVarying], varyingStep[TriVertex::NumVarying];
					for (int i = 0; i < TriVertex::NumVarying; i++)
					{
						varying[i] = varyingTL[i] + varyingBL[i] * iy;
						varyingStep[i] = (varyingTR[i] + varyingBR[i] * iy - varying[i]) * (1.0f / q);
					}

					if (!thread->skipped_by_thread(y + iy))
					{
						for (int ix = x; ix < x + q; ix++)
						{
							bool visible = ix >= clipleft && ix <= clipright && (cliptop[ix] <= y + iy) && (clipbottom[ix] > y + iy);

							if (CX1 > 0 && CX2 > 0 && CX3 > 0 && visible)
							{
								uint32_t ufrac = (uint32_t)((varying[0] - floor(varying[0])) * 0x100000000LL);
								uint32_t vfrac = (uint32_t)((varying[1] - floor(varying[1])) * 0x100000000LL);

								uint32_t upos = ((ufrac >> 16) * textureWidth) >> 16;
								uint32_t vpos = ((vfrac >> 16) * textureHeight) >> 16;
								uint32_t uvoffset = upos * textureHeight + vpos;

								uint32_t fg = texturePixels[uvoffset];
								uint32_t fg_red = (RPART(fg) * light) >> 8;
								uint32_t fg_green = (GPART(fg) * light) >> 8;
								uint32_t fg_blue = (BPART(fg) * light) >> 8;
								uint32_t fg_alpha = APART(fg);

								if (fg_alpha > 127)
									buffer[ix] = 0xff000000 | (fg_red << 16) | (fg_green << 8) | fg_blue;
							}

							for (int i = 0; i < TriVertex::NumVarying; i++)
								varying[i] += varyingStep[i];

							CX1 -= FDY12;
							CX2 -= FDY23;
							CX3 -= FDY31;
						}
					}

					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;

					buffer += pitch;
				}
			}
		}

		dest += q * pitch;
	}
}

void ScreenTriangleDrawer::fill32(const ScreenTriangleDrawerArgs *args, DrawerThread *thread)
{
	uint32_t *dest = (uint32_t *)args->dest;
	int pitch = args->pitch;
	const TriVertex &v1 = *args->v1;
	const TriVertex &v2 = *args->v2;
	const TriVertex &v3 = *args->v3;
	int clipleft = args->clipleft;
	int clipright = args->clipright;
	const short *cliptop = args->cliptop;
	const short *clipbottom = args->clipbottom;
	int solidcolor = args->solidcolor;

	// 28.4 fixed-point coordinates
	const int Y1 = (int)round(16.0f * v1.y);
	const int Y2 = (int)round(16.0f * v2.y);
	const int Y3 = (int)round(16.0f * v3.y);

	const int X1 = (int)round(16.0f * v1.x);
	const int X2 = (int)round(16.0f * v2.x);
	const int X3 = (int)round(16.0f * v3.x);

	// Deltas
	const int DX12 = X1 - X2;
	const int DX23 = X2 - X3;
	const int DX31 = X3 - X1;

	const int DY12 = Y1 - Y2;
	const int DY23 = Y2 - Y3;
	const int DY31 = Y3 - Y1;

	// Fixed-point deltas
	const int FDX12 = DX12 << 4;
	const int FDX23 = DX23 << 4;
	const int FDX31 = DX31 << 4;

	const int FDY12 = DY12 << 4;
	const int FDY23 = DY23 << 4;
	const int FDY31 = DY31 << 4;

	// Bounding rectangle
	int clipymin = cliptop[clipleft];
	int clipymax = clipbottom[clipleft];
	for (int i = clipleft + 1; i <= clipright; i++)
	{
		clipymin = MIN(clipymin, (int)cliptop[i]);
		clipymax = MAX(clipymax, (int)clipbottom[i]);
	}
	int minx = MAX((MIN(MIN(X1, X2), X3) + 0xF) >> 4, clipleft);
	int maxx = MIN((MAX(MAX(X1, X2), X3) + 0xF) >> 4, clipright);
	int miny = MAX((MIN(MIN(Y1, Y2), Y3) + 0xF) >> 4, clipymin);
	int maxy = MIN((MAX(MAX(Y1, Y2), Y3) + 0xF) >> 4, clipymax - 1);
	if (minx >= maxx || miny >= maxy)
		return;

	// Block size, standard 8x8 (must be power of two)
	const int q = 8;

	// Start in corner of 8x8 block
	minx &= ~(q - 1);
	miny &= ~(q - 1);

	dest += miny * pitch;

	// Half-edge constants
	int C1 = DY12 * X1 - DX12 * Y1;
	int C2 = DY23 * X2 - DX23 * Y2;
	int C3 = DY31 * X3 - DX31 * Y3;

	// Correct for fill convention
	if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
	if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
	if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

	// Loop through blocks
	for (int y = miny; y < maxy; y += q)
	{
		for (int x = minx; x < maxx; x += q)
		{
			// Corners of block
			int x0 = x << 4;
			int x1 = (x + q - 1) << 4;
			int y0 = y << 4;
			int y1 = (y + q - 1) << 4;

			// Evaluate half-space functions
			bool a00 = C1 + DX12 * y0 - DY12 * x0 > 0;
			bool a10 = C1 + DX12 * y0 - DY12 * x1 > 0;
			bool a01 = C1 + DX12 * y1 - DY12 * x0 > 0;
			bool a11 = C1 + DX12 * y1 - DY12 * x1 > 0;
			int a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);

			bool b00 = C2 + DX23 * y0 - DY23 * x0 > 0;
			bool b10 = C2 + DX23 * y0 - DY23 * x1 > 0;
			bool b01 = C2 + DX23 * y1 - DY23 * x0 > 0;
			bool b11 = C2 + DX23 * y1 - DY23 * x1 > 0;
			int b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);

			bool c00 = C3 + DX31 * y0 - DY31 * x0 > 0;
			bool c10 = C3 + DX31 * y0 - DY31 * x1 > 0;
			bool c01 = C3 + DX31 * y1 - DY31 * x0 > 0;
			bool c11 = C3 + DX31 * y1 - DY31 * x1 > 0;
			int c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);

			// Skip block when outside an edge
			if (a == 0x0 || b == 0x0 || c == 0x0) continue;

			// Check if block needs clipping
			int clipcount = 0;
			for (int ix = x; ix < x + q; ix++)
			{
				clipcount += (clipleft > ix) || (clipright < ix) || (cliptop[ix] > y) || (clipbottom[ix] <= y + q - 1);
			}

			uint32_t *buffer = dest;

			// Accept whole block when totally covered
			if (a == 0xF && b == 0xF && c == 0xF && clipcount == 0)
			{
				for (int iy = 0; iy < q; iy++)
				{
					if (!thread->skipped_by_thread(y + iy))
					{
						for (int ix = x; ix < x + q; ix++)
						{
							buffer[ix] = solidcolor;
						}
					}

					buffer += pitch;
				}
			}
			else // Partially covered block
			{
				int CY1 = C1 + DX12 * y0 - DY12 * x0;
				int CY2 = C2 + DX23 * y0 - DY23 * x0;
				int CY3 = C3 + DX31 * y0 - DY31 * x0;

				for (int iy = 0; iy < q; iy++)
				{
					int CX1 = CY1;
					int CX2 = CY2;
					int CX3 = CY3;

					if (!thread->skipped_by_thread(y + iy))
					{
						for (int ix = x; ix < x + q; ix++)
						{
							bool visible = ix >= clipleft && ix <= clipright && (cliptop[ix] <= y + iy) && (clipbottom[ix] > y + iy);

							if (CX1 > 0 && CX2 > 0 && CX3 > 0 && visible)
							{
								buffer[ix] = solidcolor;
							}

							CX1 -= FDY12;
							CX2 -= FDY23;
							CX3 -= FDY31;
						}
					}

					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;

					buffer += pitch;
				}
			}
		}

		dest += q * pitch;
	}
}

float ScreenTriangleDrawer::gradx(float x0, float y0, float x1, float y1, float x2, float y2, float c0, float c1, float c2)
{
	float top = (c1 - c2) * (y0 - y2) - (c0 - c2) * (y1 - y2);
	float bottom = (x1 - x2) * (y0 - y2) - (x0 - x2) * (y1 - y2);
	return top / bottom;
}

float ScreenTriangleDrawer::grady(float x0, float y0, float x1, float y1, float x2, float y2, float c0, float c1, float c2)
{
	float top = (c1 - c2) * (x0 - x2) - (c0 - c2) * (x1 - x2);
	float bottom = -((x1 - x2) * (y0 - y2) - (x0 - x2) * (y1 - y2));
	return top / bottom;
}

/////////////////////////////////////////////////////////////////////////////

TriMatrix TriMatrix::null()
{
	TriMatrix m;
	memset(m.matrix, 0, sizeof(m.matrix));
	return m;
}

TriMatrix TriMatrix::identity()
{
	TriMatrix m = null();
	m.matrix[0] = 1.0f;
	m.matrix[5] = 1.0f;
	m.matrix[10] = 1.0f;
	m.matrix[15] = 1.0f;
	return m;
}

TriMatrix TriMatrix::translate(float x, float y, float z)
{
	TriMatrix m = identity();
	m.matrix[0 + 3 * 4] = x;
	m.matrix[1 + 3 * 4] = y;
	m.matrix[2 + 3 * 4] = z;
	return m;
}

TriMatrix TriMatrix::scale(float x, float y, float z)
{
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = x;
	m.matrix[1 + 1 * 4] = y;
	m.matrix[2 + 2 * 4] = z;
	m.matrix[3 + 3 * 4] = 1;
	return m;
}

TriMatrix TriMatrix::rotate(float angle, float x, float y, float z)
{
	float c = cosf(angle);
	float s = sinf(angle);
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = (x*x*(1.0f - c) + c);
	m.matrix[0 + 1 * 4] = (x*y*(1.0f - c) - z*s);
	m.matrix[0 + 2 * 4] = (x*z*(1.0f - c) + y*s);
	m.matrix[1 + 0 * 4] = (y*x*(1.0f - c) + z*s);
	m.matrix[1 + 1 * 4] = (y*y*(1.0f - c) + c);
	m.matrix[1 + 2 * 4] = (y*z*(1.0f - c) - x*s);
	m.matrix[2 + 0 * 4] = (x*z*(1.0f - c) - y*s);
	m.matrix[2 + 1 * 4] = (y*z*(1.0f - c) + x*s);
	m.matrix[2 + 2 * 4] = (z*z*(1.0f - c) + c);
	m.matrix[3 + 3 * 4] = 1.0f;
	return m;
}

TriMatrix TriMatrix::swapYZ()
{
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = 1.0f;
	m.matrix[1 + 2 * 4] = 1.0f;
	m.matrix[2 + 1 * 4] = -1.0f;
	m.matrix[3 + 3 * 4] = 1.0f;
	return m;
}

TriMatrix TriMatrix::perspective(float fovy, float aspect, float z_near, float z_far)
{
	float f = (float)(1.0 / tan(fovy * M_PI / 360.0));
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = f / aspect;
	m.matrix[1 + 1 * 4] = f;
	m.matrix[2 + 2 * 4] = (z_far + z_near) / (z_near - z_far);
	m.matrix[2 + 3 * 4] = (2.0f * z_far * z_near) / (z_near - z_far);
	m.matrix[3 + 2 * 4] = -1.0f;
	return m;
}

TriMatrix TriMatrix::frustum(float left, float right, float bottom, float top, float near, float far)
{
	float a = (right + left) / (right - left);
	float b = (top + bottom) / (top - bottom);
	float c = -(far + near) / (far - near);
	float d = -(2.0f * far) / (far - near);
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = 2.0f * near / (right - left);
	m.matrix[1 + 1 * 4] = 2.0f * near / (top - bottom);
	m.matrix[0 + 2 * 4] = a;
	m.matrix[1 + 2 * 4] = b;
	m.matrix[2 + 2 * 4] = c;
	m.matrix[2 + 3 * 4] = d;
	m.matrix[3 + 2 * 4] = -1;
	return m;
}

TriMatrix TriMatrix::worldToView()
{
	TriMatrix m = null();
	m.matrix[0 + 0 * 4] = (float)ViewSin;
	m.matrix[0 + 1 * 4] = (float)-ViewCos;
	m.matrix[1 + 2 * 4] = 1.0f;
	m.matrix[2 + 0 * 4] = (float)-ViewCos;
	m.matrix[2 + 1 * 4] = (float)-ViewSin;
	m.matrix[3 + 3 * 4] = 1.0f;
	return m * translate((float)-ViewPos.X, (float)-ViewPos.Y, (float)-ViewPos.Z);
}

TriMatrix TriMatrix::viewToClip()
{
	float near = 5.0f;
	float far = 65536.0f;
	float width = (float)(FocalTangent * near);
	float top = (float)(CenterY / InvZtoScale * near);
	float bottom = (float)(top - viewheight / InvZtoScale * near);
	return frustum(-width, width, bottom, top, near, far);
}

TriMatrix TriMatrix::operator*(const TriMatrix &mult) const
{
	TriMatrix result;
	for (int x = 0; x < 4; x++)
	{
		for (int y = 0; y < 4; y++)
		{
			result.matrix[x + y * 4] =
				matrix[0 * 4 + x] * mult.matrix[y * 4 + 0] +
				matrix[1 * 4 + x] * mult.matrix[y * 4 + 1] +
				matrix[2 * 4 + x] * mult.matrix[y * 4 + 2] +
				matrix[3 * 4 + x] * mult.matrix[y * 4 + 3];
		}
	}
	return result;
}

TriVertex TriMatrix::operator*(TriVertex v) const
{
	float vx = matrix[0 * 4 + 0] * v.x + matrix[1 * 4 + 0] * v.y + matrix[2 * 4 + 0] * v.z + matrix[3 * 4 + 0] * v.w;
	float vy = matrix[0 * 4 + 1] * v.x + matrix[1 * 4 + 1] * v.y + matrix[2 * 4 + 1] * v.z + matrix[3 * 4 + 1] * v.w;
	float vz = matrix[0 * 4 + 2] * v.x + matrix[1 * 4 + 2] * v.y + matrix[2 * 4 + 2] * v.z + matrix[3 * 4 + 2] * v.w;
	float vw = matrix[0 * 4 + 3] * v.x + matrix[1 * 4 + 3] * v.y + matrix[2 * 4 + 3] * v.z + matrix[3 * 4 + 3] * v.w;
	v.x = vx;
	v.y = vy;
	v.z = vz;
	v.w = vw;
	return v;
}

/////////////////////////////////////////////////////////////////////////////

DrawTrianglesCommand::DrawTrianglesCommand(const TriUniforms &uniforms, const TriVertex *vinput, int vcount, TriangleDrawMode mode, bool ccw, int clipleft, int clipright, const short *clipdata, const uint8_t *texturePixels, int textureWidth, int textureHeight, int solidcolor)
	: uniforms(uniforms), vinput(vinput), vcount(vcount), mode(mode), ccw(ccw), clipleft(clipleft), clipright(clipright), clipdata(clipdata), texturePixels(texturePixels), textureWidth(textureWidth), textureHeight(textureHeight), solidcolor(solidcolor)
{
}

void DrawTrianglesCommand::Execute(DrawerThread *thread)
{
	int cliplength = clipright - clipleft + 1;
	for (int i = 0; i < cliplength; i++)
	{
		thread->triangle_clip_top[clipleft + i] = clipdata[i];
		thread->triangle_clip_bottom[clipleft + i] = clipdata[cliplength + i];
	}

	TriangleDrawer::draw_arrays(
		uniforms, vinput, vcount, mode, ccw,
		clipleft, clipright, thread->triangle_clip_top, thread->triangle_clip_bottom,
		texturePixels, textureWidth, textureHeight, solidcolor,
		thread, texturePixels ? ScreenTriangleDrawer::draw32 : ScreenTriangleDrawer::fill32);
}

FString DrawTrianglesCommand::DebugInfo()
{
	return "DrawTriangles";
}