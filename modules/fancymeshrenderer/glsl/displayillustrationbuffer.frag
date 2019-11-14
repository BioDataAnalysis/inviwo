/*********************************************************************************
 *
 * Inviwo - Interactive Visualization Workshop
 *
 * Copyright (c) 2019 Inviwo Foundation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *********************************************************************************/

/**
 * Blends the final color of the illustration buffer
 */

#include "illustrationbuffer.hglsl"

//Whole number pixel offsets (not necessary just to test the layout keyword !)
layout(pixel_center_integer) in vec4 gl_FragCoord;
//Input interpolated fragment position
smooth in vec4 fragPos;

layout(std430, binding=0) buffer colorBufferIn
{
    vec2 colorIn[]; //alpha+rgb
};
layout(std430, binding=1) buffer smoothingBufferIn
{
    vec2 smoothingIn[]; //beta + gamma
};

uniform vec4 edgeColor = vec4(0, 0, 0, 1);
uniform float haloStrength = 0.4;

void main(void) {
	ivec2 coords=ivec2(gl_FragCoord.xy);
	
	if(coords.x>=0 && coords.y>=0 
	    && coords.x<screenSize.x 
	    && coords.y<screenSize.y ){

        uint count = imageLoad(illustrationBufferCountImg, coords).x;
        vec4 color = vec4(0,0,0,0);
        if (count > 0) {
            uint start = imageLoad(illustrationBufferIdxImg, coords).x;
            for (int i=0; i<count; ++i) {
                //fetch properties from the scalar fields
                vec3 baseColor = uncompressColor(floatBitsToInt(colorIn[i+start].y));
                float alpha = colorIn[i+start].x;
                alpha = clamp(alpha, 0, 1);
                float beta = smoothingIn[i+start].x;
                float gamma = smoothingIn[i+start].y;

                //blend them together
                float alphaHat = (1-gamma*haloStrength*(1-beta))*(alpha+(1-alpha)*beta*edgeColor.a);
                baseColor = mix(baseColor, edgeColor.rgb, beta*edgeColor.a);
                color.rgb = color.rgb + (1-color.a)*alphaHat*baseColor;
                color.a = color.a + (1-color.a)*alphaHat;
            }
            FragData0 = color;
            return;
        }
    }
    discard;
}
