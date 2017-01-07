/*
**  Handling drawing a decal
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

#pragma once

#include "polyrenderer/drawers/poly_triangle.h"

class Vec4f;

class RenderPolyDecal
{
public:
	static void RenderWallDecals(const TriMatrix &worldToClip, const Vec4f &clipPlane, const seg_t *line, uint32_t subsectorDepth, uint32_t stencilValue);

private:
	void Render(const TriMatrix &worldToClip, const Vec4f &clipPlane, DBaseDecal *decal, const seg_t *line, uint32_t subsectorDepth, uint32_t stencilValue);
};