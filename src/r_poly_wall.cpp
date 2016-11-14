/*
**  Handling drawing a wall
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

#include <stdlib.h>
#include "templates.h"
#include "doomdef.h"
#include "sbar.h"
#include "r_data/r_translate.h"
#include "r_poly_wall.h"
#include "r_poly.h"
#include "r_sky.h" // for skyflatnum

bool RenderPolyWall::RenderLine(const TriMatrix &worldToClip, seg_t *line, sector_t *frontsector, uint32_t subsectorDepth, std::vector<PolyTranslucentObject> &translucentWallsOutput)
{
	double frontceilz1 = frontsector->ceilingplane.ZatPoint(line->v1);
	double frontfloorz1 = frontsector->floorplane.ZatPoint(line->v1);
	double frontceilz2 = frontsector->ceilingplane.ZatPoint(line->v2);
	double frontfloorz2 = frontsector->floorplane.ZatPoint(line->v2);

	RenderPolyWall wall;
	wall.Line = line;
	wall.Colormap = frontsector->ColorMap;
	wall.Masked = false;
	wall.SubsectorDepth = subsectorDepth;

	if (line->backsector == nullptr)
	{
		if (line->sidedef)
		{
			wall.SetCoords(line->v1->fPos(), line->v2->fPos(), frontceilz1, frontfloorz1, frontceilz2, frontfloorz2);
			wall.TopZ = frontceilz1;
			wall.BottomZ = frontfloorz1;
			wall.UnpeggedCeil = frontceilz1;
			wall.Texpart = side_t::mid;
			wall.Render(worldToClip);
			return true;
		}
	}
	else
	{
		sector_t *backsector = (line->backsector != line->frontsector) ? line->backsector : line->frontsector;

		double backceilz1 = backsector->ceilingplane.ZatPoint(line->v1);
		double backfloorz1 = backsector->floorplane.ZatPoint(line->v1);
		double backceilz2 = backsector->ceilingplane.ZatPoint(line->v2);
		double backfloorz2 = backsector->floorplane.ZatPoint(line->v2);

		double topceilz1 = frontceilz1;
		double topceilz2 = frontceilz2;
		double topfloorz1 = MIN(backceilz1, frontceilz1);
		double topfloorz2 = MIN(backceilz2, frontceilz2);
		double bottomceilz1 = MAX(frontfloorz1, backfloorz1);
		double bottomceilz2 = MAX(frontfloorz2, backfloorz2);
		double bottomfloorz1 = frontfloorz1;
		double bottomfloorz2 = frontfloorz2;
		double middleceilz1 = topfloorz1;
		double middleceilz2 = topfloorz2;
		double middlefloorz1 = MIN(bottomceilz1, middleceilz1);
		double middlefloorz2 = MIN(bottomceilz2, middleceilz2);

		bool bothSkyCeiling = frontsector->GetTexture(sector_t::ceiling) == skyflatnum && backsector->GetTexture(sector_t::ceiling) == skyflatnum;

		if ((topceilz1 > topfloorz1 || topceilz2 > topfloorz2) && line->sidedef && !bothSkyCeiling)
		{
			wall.SetCoords(line->v1->fPos(), line->v2->fPos(), topceilz1, topfloorz1, topceilz2, topfloorz2);
			wall.TopZ = topceilz1;
			wall.BottomZ = topfloorz1;
			wall.UnpeggedCeil = topceilz1;
			wall.Texpart = side_t::top;
			wall.Render(worldToClip);
		}

		if ((bottomfloorz1 < bottomceilz1 || bottomfloorz2 < bottomceilz2) && line->sidedef)
		{
			wall.SetCoords(line->v1->fPos(), line->v2->fPos(), bottomceilz1, bottomfloorz2, bottomceilz2, bottomfloorz2);
			wall.TopZ = bottomceilz1;
			wall.BottomZ = bottomfloorz2;
			wall.UnpeggedCeil = topceilz1;
			wall.Texpart = side_t::bottom;
			wall.Render(worldToClip);
		}

		if (line->sidedef)
		{
			FTexture *midtex = TexMan(line->sidedef->GetTexture(side_t::mid), true);
			if (midtex && midtex->UseType != FTexture::TEX_Null)
			{
				wall.SetCoords(line->v1->fPos(), line->v2->fPos(), middleceilz1, middlefloorz1, middleceilz2, middlefloorz2);
				wall.TopZ = middleceilz1;
				wall.BottomZ = middlefloorz1;
				wall.UnpeggedCeil = topceilz1;
				wall.Texpart = side_t::mid;
				wall.Masked = true;
				translucentWallsOutput.push_back({ wall });
			}
		}
	}
	return false;
}

void RenderPolyWall::Render(const TriMatrix &worldToClip)
{
	FTexture *tex = GetTexture();
	if (!tex)
		return;

	PolyWallTextureCoords texcoords(tex, Line, Texpart, TopZ, BottomZ, UnpeggedCeil);

	TriVertex *vertices = PolyVertexBuffer::GetVertices(4);
	if (!vertices)
		return;

	vertices[0].x = (float)v1.X;
	vertices[0].y = (float)v1.Y;
	vertices[0].z = (float)ceil1;
	vertices[0].w = 1.0f;
	vertices[0].varying[0] = (float)texcoords.u1;
	vertices[0].varying[1] = (float)texcoords.v1;

	vertices[1].x = (float)v2.X;
	vertices[1].y = (float)v2.Y;
	vertices[1].z = (float)ceil2;
	vertices[1].w = 1.0f;
	vertices[1].varying[0] = (float)texcoords.u2;
	vertices[1].varying[1] = (float)texcoords.v1;

	vertices[2].x = (float)v2.X;
	vertices[2].y = (float)v2.Y;
	vertices[2].z = (float)floor2;
	vertices[2].w = 1.0f;
	vertices[2].varying[0] = (float)texcoords.u2;
	vertices[2].varying[1] = (float)texcoords.v2;

	vertices[3].x = (float)v1.X;
	vertices[3].y = (float)v1.Y;
	vertices[3].z = (float)floor1;
	vertices[3].w = 1.0f;
	vertices[3].varying[0] = (float)texcoords.u1;
	vertices[3].varying[1] = (float)texcoords.v2;

	TriUniforms uniforms;
	uniforms.objectToClip = worldToClip;
	uniforms.light = (uint32_t)(GetLightLevel() / 255.0f * 256.0f);
	uniforms.flags = 0;
	uniforms.subsectorDepth = SubsectorDepth;

	PolyDrawArgs args;
	args.uniforms = uniforms;
	args.vinput = vertices;
	args.vcount = 4;
	args.mode = TriangleDrawMode::Fan;
	args.ccw = true;
	args.clipleft = 0;
	args.cliptop = 0;
	args.clipright = viewwidth;
	args.clipbottom = viewheight;
	args.stenciltestvalue = 0;
	args.stencilwritevalue = 1;
	args.SetTexture(tex);

	if (!Masked)
	{
		PolyTriangleDrawer::draw(args, TriDrawVariant::Draw);
		PolyTriangleDrawer::draw(args, TriDrawVariant::Stencil);
	}
	else
	{
		PolyTriangleDrawer::draw(args, TriDrawVariant::DrawSubsector);
	}
}

FTexture *RenderPolyWall::GetTexture()
{
	FTexture *tex = TexMan(Line->sidedef->GetTexture(Texpart), true);
	if (tex == nullptr || tex->UseType == FTexture::TEX_Null)
		return nullptr;
	else
		return tex;
}

int RenderPolyWall::GetLightLevel()
{
	if (fixedlightlev >= 0 || fixedcolormap)
	{
		return 255;
	}
	else
	{
		bool foggy = false;
		int actualextralight = foggy ? 0 : extralight << 4;
		return Line->sidedef->GetLightLevel(foggy, Line->frontsector->lightlevel) + actualextralight;
	}
}

/////////////////////////////////////////////////////////////////////////////

PolyWallTextureCoords::PolyWallTextureCoords(FTexture *tex, const seg_t *line, side_t::ETexpart texpart, double topz, double bottomz, double unpeggedceil)
{
	CalcU(tex, line, texpart);
	CalcV(tex, line, texpart, topz, bottomz, unpeggedceil);
}

void PolyWallTextureCoords::CalcU(FTexture *tex, const seg_t *line, side_t::ETexpart texpart)
{
	double lineLength = line->sidedef->TexelLength;
	double lineStart = 0.0;

	bool entireSegment = ((line->linedef->v1 == line->v1) && (line->linedef->v2 == line->v2) || (line->linedef->v2 == line->v1) && (line->linedef->v1 == line->v2));
	if (!entireSegment)
	{
		lineLength = (line->v2->fPos() - line->v1->fPos()).Length();
		lineStart = (line->v1->fPos() - line->linedef->v1->fPos()).Length();
	}

	int texWidth = tex->GetWidth();
	double uscale = line->sidedef->GetTextureXScale(texpart) * tex->Scale.X;
	u1 = lineStart + line->sidedef->GetTextureXOffset(texpart);
	u2 = u1 + lineLength;
	u1 *= uscale;
	u2 *= uscale;
	u1 /= texWidth;
	u2 /= texWidth;
}

void PolyWallTextureCoords::CalcV(FTexture *tex, const seg_t *line, side_t::ETexpart texpart, double topz, double bottomz, double unpeggedceil)
{
	double vscale = line->sidedef->GetTextureYScale(texpart) * tex->Scale.Y;

	double yoffset = line->sidedef->GetTextureYOffset(texpart);
	if (tex->bWorldPanning)
		yoffset *= vscale;

	switch (texpart)
	{
	default:
	case side_t::mid:
		CalcVMidPart(tex, line, topz, bottomz, vscale, yoffset);
		break;
	case side_t::top:
		CalcVTopPart(tex, line, topz, bottomz, vscale, yoffset);
		break;
	case side_t::bottom:
		CalcVBottomPart(tex, line, topz, bottomz, unpeggedceil, vscale, yoffset);
		break;
	}

	int texHeight = tex->GetHeight();
	v1 /= texHeight;
	v2 /= texHeight;
}

void PolyWallTextureCoords::CalcVTopPart(FTexture *tex, const seg_t *line, double topz, double bottomz, double vscale, double yoffset)
{
	bool pegged = (line->linedef->flags & ML_DONTPEGTOP) == 0;
	if (pegged) // bottom to top
	{
		int texHeight = tex->GetHeight();
		v1 = -yoffset;
		v2 = v1 + (topz - bottomz);
		v1 *= vscale;
		v2 *= vscale;
		v1 = texHeight - v1;
		v2 = texHeight - v2;
		std::swap(v1, v2);
	}
	else // top to bottom
	{
		v1 = yoffset;
		v2 = v1 + (topz - bottomz);
		v1 *= vscale;
		v2 *= vscale;
	}
}

void PolyWallTextureCoords::CalcVMidPart(FTexture *tex, const seg_t *line, double topz, double bottomz, double vscale, double yoffset)
{
	bool pegged = (line->linedef->flags & ML_DONTPEGBOTTOM) == 0;
	if (pegged) // top to bottom
	{
		v1 = yoffset;
		v2 = v1 + (topz - bottomz);
		v1 *= vscale;
		v2 *= vscale;
	}
	else // bottom to top
	{
		int texHeight = tex->GetHeight();
		v1 = yoffset;
		v2 = v1 + (topz - bottomz);
		v1 *= vscale;
		v2 *= vscale;
		v1 = texHeight - v1;
		v2 = texHeight - v2;
		std::swap(v1, v2);
	}
}

void PolyWallTextureCoords::CalcVBottomPart(FTexture *tex, const seg_t *line, double topz, double bottomz, double unpeggedceil, double vscale, double yoffset)
{
	bool pegged = (line->linedef->flags & ML_DONTPEGBOTTOM) == 0;
	if (pegged) // top to bottom
	{
		v1 = yoffset;
		v2 = v1 + (topz - bottomz);
		v1 *= vscale;
		v2 *= vscale;
	}
	else
	{
		v1 = yoffset + (unpeggedceil - topz);
		v2 = v1 + (topz - bottomz);
		v1 *= vscale;
		v2 *= vscale;
	}
}