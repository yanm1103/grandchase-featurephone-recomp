#version 450

/*
 * SDL_GPU port of ScaleFX pass 4 (GLSL 450 / Vulkan) for use as an SDL_GPURenderState custom fragment shader.
 * Pass 4: outputs the 3x3 subpixels (per subpixel, picks one of E,D,D0,F,F0,B,B0,H,H0 of the Original).
 *
 * Samplers (set = 2, always NEAREST filter + CLAMP_TO_EDGE):
 *   binding 0  Source           = PassPrev1 (output of pass 3: tags), NEAREST
 *   binding 1  PassPrev5Texture = Original (the game texture, same size as Source), NEAREST
 * Params (set = 3, binding = 0): none (no uniform block; do not push uniforms)
 * Render target: 3*gw x 3*gh, RGBA8 is enough (final colours). The quad must cover the WHOLE target (v_uv 0..1 spans 3x3 output pixels per source texel);
 *   then show it on screen with LINEAR filtering (what Miki's World does).
 * Blend: the pass stores DATA in all four channels (alpha included): draw with SDL_BLENDMODE_NONE.
 * Requires Source and PassPrev5Texture to have the same dimensions (as in libretro).
 * Texel offsets use textureOffset() / textureSize(), so no size uniforms are needed.
 * The fixed SDL vertex shader provides v_color (unused) and v_uv (0..1, (0,0) = top-left).
 * Algorithm: ScaleFX by Sp00kyFox (MIT), libretro glsl version, adapted. Original header and license follow.
 */

/*
	ScaleFX - Pass 4
	by Sp00kyFox, 2017-03-01

Filter:	Nearest
Scale:	3x

ScaleFX is an edge interpolation algorithm specialized in pixel art. It was
originally intended as an improvement upon Scale3x but became a new filter in
its own right.
ScaleFX interpolates edges up to level 6 and makes smooth transitions between
different slopes. The filtered picture will only consist of colours present
in the original.

Pass 4 outputs subpixels based on previously calculated tags.


Copyright (c) 2016 Sp00kyFox - ScaleFX@web.de

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D Source;
layout(set = 2, binding = 1) uniform sampler2D PassPrev5Texture;

// libretro compatibility: the vertex shader is fixed in SDL, so TEX0.xy comes from v_uv
#define COMPAT_TEXTURE texture
#define FragColor o_color
#define vTexCoord v_uv
#define SourceSize vec4(vec2(textureSize(Source, 0)), 1.0 / vec2(textureSize(Source, 0)))

// extract corners
vec4 loadCrn(vec4 x){
	return floor(mod(x*80. + 0.5, 9.));
}

// extract mids
vec4 loadMid(vec4 x){
	return floor(mod(x*8.888888 + 0.055555, 9.));
}

void main()
{
	/*	grid		corners		mids

		  B		x   y	  	  x
		D E F				w   y
		  H		w   z	  	  z
	*/


	// read data
	vec4 E = COMPAT_TEXTURE(Source, vTexCoord);

	// extract data
	vec4 crn = loadCrn(E);
	vec4 mid = loadMid(E);

	// determine subpixel
	vec2 fp = floor(3.0 * fract(vTexCoord * SourceSize.xy));
	float sp = fp.y == 0. ? (fp.x == 0. ? crn.x : fp.x == 1. ? mid.x : crn.y) : (fp.y == 1. ? (fp.x == 0. ? mid.w : fp.x == 1. ? 0. : mid.y) : (fp.x == 0. ? crn.w : fp.x == 1. ? mid.z : crn.z));

	// output coordinate - 0 = E, 1 = D, 2 = D0, 3 = F, 4 = F0, 5 = B, 6 = B0, 7 = H, 8 = H0
	vec2 res = sp == 0. ? vec2(0.,0.) : sp == 1. ? vec2(-1.,0.) : sp == 2. ? vec2(-2.,0.) : sp == 3. ? vec2(1.,0.) : sp == 4. ? vec2(2.,0.) : sp == 5. ? vec2(0,-1) : sp == 6. ? vec2(0.,-2.) : sp == 7. ? vec2(0.,1.) : vec2(0.,2.);

	// ouput
	FragColor = COMPAT_TEXTURE(PassPrev5Texture, vTexCoord + res / SourceSize.xy);
} 
