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

#ifndef IVW_FRAGMENTLISTRENDERER_H
#define IVW_FRAGMENTLISTRENDERER_H

#include <modules/fancymeshrenderer/fancymeshrenderermoduledefine.h>
#include <inviwo/core/common/inviwo.h>
#include <modules/opengl/inviwoopengl.h>
#include <inviwo/core/rendering/meshdrawer.h>
#include <modules/opengl/shader/shader.h>
#include <modules/opengl/texture/texture2d.h>
#include <inviwo/core/properties/boolproperty.h>
#include <inviwo/core/properties/buttonproperty.h>
#include <inviwo/core/properties/compositeproperty.h>
#include <inviwo/core/properties/optionproperty.h>

namespace inviwo {

/**
 * \brief helper class for rendering perfect alpha-blended shapes using fragment lists.
 * Inspiration taken from
 http://blog.icare3d.org/2010/07/opengl-40-abuffer-v20-linked-lists-of.html.
 * It requires OpenGL 4.2.
 *
 * Any objects can be rendered with this framework in the following way:
  <pre>
  1. Render opaque objects normally
  2. Call FragmentListRenderer::prePass(...)
  3. For each transparent object:
     a) Include ABufferLinkedList.hglsl in the fragment shader
     b) Use the following snipped in the fragment shader:
        abufferRender(ivec2(gl_FragCoord.xy), depth, fragColor);
        discard;
     c) Assign additional shader uniforms with FragmentListRenderer::setShaderUniforms(shader)
     d) Render the object with depth test but without depth write
  4. Call FragmetnListRenderer::postPass(...)
     If this returns <code>false</code>, not enough space for all pixels
     was available. Repeat from step 2.
  </pre>
 *
 */
class IVW_MODULE_FANCYMESHRENDERER_API FragmentListRenderer {
public:
    FragmentListRenderer();
    ~FragmentListRenderer();

    /**
     * \brief Starts the rendering of transparent objects using fragment lists.
     * It resets all counters and allocated the index textures of the given screen size.
     * This has to be called each frame before objects can be rendered with the fragment lists.
     * \param screenSize the current screen size
     */
    void prePass(const size2_t& screenSize);

    /**
     * \brief Sets the shader uniforms required by the fragment list renderer.
     * The uniforms are defined in <code>ABufferLinkedList.hglsl</code>
     * \param shader the shader of the object to be rendered
     */
    void setShaderUniforms(Shader& shader) const;

    /**
     * \brief Finishes the fragment list pass and renders the final result.
     * This sorts the fragment lists per pixel and outputs the blended color.
     * \param useIllustrationBuffer Set to true if the illustration buffer
     * should be enabled
     * \param debug If set to true, debug output is printed to <code>cout</code>.
     * Warning: very text heavy, use only for small screen sizes.
     * \return <code>true</code> if successfull, <code>false</code> if not enough
     * space for all fragments was available and the procedure should be repeated.
     */
    bool postPass(bool useIllustrationBuffer, bool debug = false);

    struct IllustrationBufferSettings {
        vec3 edgeColor_;
        float edgeStrength_;
        float haloStrength_;
        int smoothingSteps_;
        float edgeSmoothing_;
        float haloSmoothing_;
    };
    void setIllustrationBufferSettings(const IllustrationBufferSettings& settings) {
        illustrationBufferSettings_ = settings;
    }

    /**
     * \brief Tests if fragment lists are supported by the current opengl context.
     * Fragment lists require OpenGL 4.3
     * \return true iff they are supported
     */
    static bool supportsFragmentLists();
    /**
     * \brief Tests if the illustration buffer are supported and can therefore be enabled.
     * The Illustration Buffer requires OpenGL 4.6 or OpenGL 4.5 with the extension
     * "GL_ARB_shader_atomic_counter_ops". \return true iff they are supported
     */
    static bool supportsIllustrationBuffer();

private:
    void initShaders();
    void initBuffers(const size2_t& screenSize);
    void assignUniforms(Shader& shader) const;
    void drawQuad() const;
    void debugFragmentLists(GLuint numFrags);
    void initIllustrationBuffer();
    void fillIllustrationBuffer();
    void processIllustrationBuffer();
    void drawIllustrationBuffer();
    void assignIllustrationBufferUniforms(Shader& shader);
    void debugIllustrationBuffer(GLuint numFrags);

    size2_t screenSize_;
    size_t fragmentSize_;
    size_t oldFragmentSize_;

    // basic fragment lists
    Texture2D* abufferIdxImg_;
    TextureUnit* abufferIdxUnit_;
    GLuint atomicCounter_;
    GLuint pixelBuffer_;
    GLuint totalFragmentQuery_;
    Shader clearShader_;
    Shader displayShader_;

    // illustration buffers
    size2_t illustrationBufferOldScreenSize_;
    size_t illustrationBufferOldFragmentSize_;
    Texture2D* illustrationBufferIdxImg_;
    TextureUnit* illustrationBufferIdxUnit_;
    Texture2D* illustrationBufferCountImg_;
    TextureUnit* illustrationBufferCountUnit_;
    GLuint illustrationColorBuffer_;
    GLuint illustrationSurfaceInfoBuffer_;
    GLuint illustrationSmoothingBuffer_[2];
    int activeIllustrationSmoothingBuffer_;
    Shader fillIllustrationBufferShader_;
    Shader resolveNeighborsIllustrationBufferShader_;
    Shader drawIllustrationBufferShader_;
    Shader smoothIllustrationBufferShader_;
    IllustrationBufferSettings illustrationBufferSettings_;
};

}  // namespace inviwo

#endif
