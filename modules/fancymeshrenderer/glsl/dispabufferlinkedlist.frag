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
 * Fast Single-pass A-Buffer using OpenGL 4.0 V2.0
 * Copyright Cyril Crassin, July 2010
**/

//#extension GL_NV_gpu_shader5 : enable
//#extension GL_EXT_shader_image_load_store : enable
//#extension GL_NV_shader_buffer_load : enable
//#extension GL_NV_shader_buffer_store : enable
//#extension GL_EXT_bindable_uniform : enable

#include "abufferlinkedlist.hglsl"
#include "abuffersort.hglsl"

// How should the stuff be rendered? (Debugging options)
#define ABUFFER_DISPNUMFRAGMENTS 0
#define ABUFFER_RESOLVE_USE_SORTING 1

//Whole number pixel offsets (not necessary just to test the layout keyword !)
layout(pixel_center_integer) in vec4 gl_FragCoord;

//Input interpolated fragment position
smooth in vec4 fragPos;

//Computes only the number of fragments
int getFragmentCount(uint pixelIdx);
//Keeps only closest fragment
vec4 resolveClosest(uint idx);
//Fill local memory array of fragments
void fillFragmentArray(uint idx, out int numFrag);


//Resolve A-Buffer and blend sorted fragments
void main(void) {
	ivec2 coords=ivec2(gl_FragCoord.xy);
	
	if(coords.x>=0 && coords.y>=0 
	   && coords.x<AbufferParams.screenWidth 
	   && coords.y<AbufferParams.screenHeight ){

		uint pixelIdx=getPixelLink(coords);

		if(pixelIdx > 0 ){

#if ABUFFER_DISPNUMFRAGMENTS==1
        FragData0=vec4(getFragmentCount(pixelIdx) / float(ABUFFER_SIZE));
#elif ABUFFER_RESOLVE_USE_SORTING==0	
		//If we only want the closest fragment
        vec4 p = resolveClosest(pixelIdx);
        FragData0 = uncompressPixelData(p).color;
#else
		//Copy fragments in local array
        int numFrag = 0;
		fillFragmentArray(pixelIdx, numFrag);
		//Sort fragments in local memory array
		bubbleSort(numFrag);

        //front-to-back shading
		vec4 color = vec4(0);
        for (int i=0; i<numFrag; ++i) {
            vec4 c = uncompressPixelData(fragmentList[i]).color;
            color.rgb = color.rgb + (1-color.a)*c.a*c.rgb;
            color.a = color.a + (1-color.a)*c.a;
        }
        FragData0 = color;
#endif

		}else{ //no pixel found
#if ABUFFER_DISPNUMFRAGMENTS==0
			//If no fragment, write nothing
			discard;
#else
			FragData0=vec4(0.0f);
#endif
		}
	}
}



int getFragmentCount(uint pixelIdx){
    int counter = 0;
    while(pixelIdx!=0 && counter<ABUFFER_SIZE){
        vec4 val = readPixelStorage(pixelIdx-1);
        counter++;
        pixelIdx = floatBitsToUint(val.x);
    }
    return counter;
}

vec4 resolveClosest(uint pixelIdx){

	//Search smallest z
	vec4 minFrag=vec4(0.0f, 1000000.0f, 1.0f, uintBitsToFloat(1024*1023));
	int ip=0;

	while(pixelIdx!=0 && ip<ABUFFER_SIZE){
        vec4 val = readPixelStorage(pixelIdx-1);

        if (val.y<minFrag.y) {
            minFrag = val;
        }

		pixelIdx = floatBitsToUint(val.x);

		ip++;
	}
	//Output final color for the frame buffer
	return minFrag;
}

void fillFragmentArray(uint pixelIdx, out int numFrag){
	//Load fragments into a local memory array for sorting

	int ip=0;
	while(pixelIdx!=0 && ip<ABUFFER_SIZE){
		vec4 val = readPixelStorage(pixelIdx-1);
        fragmentList[ip] = val;
        pixelIdx = floatBitsToUint(val.x);
		ip++;
	}
    numFrag = ip;
}
