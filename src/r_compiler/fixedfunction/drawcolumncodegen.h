/*
**  DrawColumn code generation
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

#include "drawercodegen.h"

enum class DrawColumnVariant
{
	Fill,
	FillAdd,
	FillAddClamp,
	FillSubClamp,
	FillRevSubClamp,
	DrawCopy,
	Draw,
	DrawAdd,
	DrawTranslated,
	DrawTlatedAdd,
	DrawShaded,
	DrawAddClamp,
	DrawAddClampTranslated,
	DrawSubClamp,
	DrawSubClampTranslated,
	DrawRevSubClamp,
	DrawRevSubClampTranslated
};

enum class DrawColumnMethod
{
	Normal,
	Rt1,
	Rt4
};

class DrawColumnCodegen : public DrawerCodegen
{
public:
	void Generate(DrawColumnVariant variant, DrawColumnMethod method, SSAValue args, SSAValue thread_data);

private:
	void Loop(DrawColumnVariant variant, DrawColumnMethod method, bool isSimpleShade);
	SSAVec4i ProcessPixel(SSAInt sample_index, SSAVec4i bgcolor, DrawColumnVariant variant, bool isSimpleShade);
	SSAVec4i ProcessPixelPal(SSAInt sample_index, SSAVec4i bgcolor, DrawColumnVariant variant, bool isSimpleShade);
	SSAVec4i Sample(SSAInt frac);
	SSAInt ColormapSample(SSAInt frac);
	SSAVec4i TranslateSample(SSAInt frac);
	SSAInt TranslateSamplePal(SSAInt frac);
	SSAVec4i Shade(SSAVec4i fgcolor, bool isSimpleShade);
	SSAVec4i ShadePal(SSAInt palIndex, bool isSimpleShade);
	bool IsPaletteInput(DrawColumnVariant variant);

	SSAStack<SSAInt> stack_index, stack_frac;

	SSAUBytePtr dest;
	SSAUBytePtr source;
	SSAUBytePtr colormap;
	SSAUBytePtr translation;
	SSAUBytePtr basecolors;
	SSAInt pitch;
	SSAInt count;
	SSAInt dest_y;
	SSAInt iscale;
	SSAInt texturefrac;
	SSAInt light;
	SSAVec4i color;
	SSAVec4i srccolor;
	SSAInt srcalpha;
	SSAInt destalpha;
	SSABool is_simple_shade;
	SSAShadeConstants shade_constants;
	SSAWorkerThread thread;
};