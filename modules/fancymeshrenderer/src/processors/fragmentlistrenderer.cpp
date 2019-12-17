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

#include <modules/fancymeshrenderer/processors/fragmentlistrenderer.h>
#include <modules/opengl/geometry/meshgl.h>
#include <modules/opengl/sharedopenglresources.h>
#include <modules/opengl/openglutils.h>
#include <modules/opengl/texture/textureutils.h>
#include <modules/opengl/image/imagegl.h>
#include <modules/opengl/openglcapabilities.h>

#include <cstdio>
#include <fmt/format.h>

namespace inviwo {
FragmentListRenderer::FragmentListRenderer()
    : screenSize_(0, 0)
    , fragmentSize_(1024)
    , oldFragmentSize_(0)
    , abufferIdxImg_(nullptr)
    , abufferIdxUnit_(nullptr)
    , atomicCounter_(0)
    , pixelBuffer_(0)
    , totalFragmentQuery_(0)
    , clearShader_("simplequad.vert", "clearabufferlinkedlist.frag", false)
    , displayShader_("simplequad.vert", "dispabufferlinkedlist.frag", false)
    , illustrationBufferOldScreenSize_(0)
    , illustrationBufferOldFragmentSize_(0)
    , illustrationBufferIdxImg_(nullptr)
    , illustrationBufferIdxUnit_(nullptr)
    , illustrationBufferCountImg_(nullptr)
    , illustrationBufferCountUnit_(nullptr)
    , illustrationColorBuffer_(0)
    , illustrationSurfaceInfoBuffer_(0)
    , illustrationSmoothingBuffer_{0, 0}
    , activeIllustrationSmoothingBuffer_(0)
    , fillIllustrationBufferShader_("simplequad.vert", "sortandfillillustrationbuffer.frag", false)
    , resolveNeighborsIllustrationBufferShader_("simplequad.vert",
                                                "resolveneighborsillustrationbuffer.frag", false)
    , drawIllustrationBufferShader_("simplequad.vert", "displayillustrationbuffer.frag", false)
    , smoothIllustrationBufferShader_("simplequad.vert", "smoothillustrationbuffer.frag", false) {
    initShaders();

    // init atomic counter
    glGenBuffers(1, &atomicCounter_);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounter_);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
    LGL_ERROR;

    // create fragment query
    glGenQueries(1, &totalFragmentQuery_);
    LGL_ERROR;
}

FragmentListRenderer::~FragmentListRenderer() {
    if (abufferIdxImg_) delete abufferIdxImg_;
    if (abufferIdxUnit_) delete abufferIdxUnit_;
    if (atomicCounter_) glDeleteBuffers(1, &atomicCounter_);
    if (pixelBuffer_) glDeleteBuffers(1, &pixelBuffer_);
    if (totalFragmentQuery_) glDeleteQueries(1, &totalFragmentQuery_);
    if (illustrationColorBuffer_) glDeleteBuffers(1, &illustrationColorBuffer_);
    if (illustrationSurfaceInfoBuffer_) glDeleteBuffers(1, &illustrationSurfaceInfoBuffer_);
    if (illustrationSmoothingBuffer_[0]) glDeleteBuffers(2, illustrationSmoothingBuffer_);
    LGL_ERROR;
}

void FragmentListRenderer::prePass(const size2_t& screenSize) {
    initBuffers(screenSize);

    // reset counter
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounter_);
    LGL_ERROR;
    GLuint v[1] = {0};
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), v);
    LGL_ERROR;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
    LGL_ERROR;

    // create unit index for index texture
    abufferIdxUnit_ = new TextureUnit();

    // clear textures
    clearShader_.activate();
    assignUniforms(clearShader_);
    drawQuad();
    clearShader_.deactivate();

    // memory barrier
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);
    LGL_ERROR;

    // start query
    glBeginQuery(GL_SAMPLES_PASSED, totalFragmentQuery_);
    LGL_ERROR;
}

void FragmentListRenderer::setShaderUniforms(Shader& shader) const { assignUniforms(shader); }

bool FragmentListRenderer::postPass(bool useIllustrationBuffer, bool debug) {
    // memory barrier
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    LGL_ERROR;

    // get query result
    GLuint numFrags = 0;
    glEndQuery(GL_SAMPLES_PASSED);
    LGL_ERROR;
    glGetQueryObjectuiv(totalFragmentQuery_, GL_QUERY_RESULT, &numFrags);
    LGL_ERROR;

    if (debug) {
        debugFragmentLists(numFrags);
    }

    // check if enough space was available
    if (numFrags > fragmentSize_) {
        // we have to resize the fragment storage buffer
        LogInfo("fragment lists resolved, pixels drawn: "
                << numFrags << ", available: " << fragmentSize_ << ", allocate space for "
                << int(1.1f * numFrags) << " pixels");
        fragmentSize_ = static_cast<size_t>(1.1f * numFrags);

        // unbind texture
        delete abufferIdxUnit_;
        abufferIdxUnit_ = nullptr;
        return false;
    }

    if (!useIllustrationBuffer) {
        // render fragment list
        displayShader_.activate();
        assignUniforms(displayShader_);
        utilgl::BlendModeState blendModeStateGL(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        drawQuad();
        displayShader_.deactivate();
    }

    // Note: illustration buffers are only called when enough space was available.
    // This allows us to drop the tests for overflow
    if (useIllustrationBuffer) {
        // 1. copy to illustration buffer
        initIllustrationBuffer();
        fillIllustrationBuffer();
    }

    // unbind texture with abuffer indices
    delete abufferIdxUnit_;
    abufferIdxUnit_ = nullptr;

    if (useIllustrationBuffer) {
        // 2. perform all the cracy post-prozessing steps
        // TODO: pass operation into this method as an enum plus optional parameters
        processIllustrationBuffer();
        drawIllustrationBuffer();

        if (debug) {
            debugIllustrationBuffer(numFrags);
        }
    }

    return true;  // success, enough storage available
}

bool FragmentListRenderer::supportsFragmentLists() {
    return OpenGLCapabilities::getOpenGLVersion() >= 430;
}

bool FragmentListRenderer::supportsIllustrationBuffer() {
    if (OpenGLCapabilities::getOpenGLVersion() >= 460)
        return true;
    else if (OpenGLCapabilities::getOpenGLVersion() >= 450)
        return OpenGLCapabilities::isExtensionSupported("GL_ARB_shader_atomic_counter_ops");
    else
        return false;
}

void FragmentListRenderer::initShaders() {
    displayShader_.getFragmentShaderObject()->addShaderDefine("COLOR_LAYER");
    displayShader_.build();
    clearShader_.build();
    fillIllustrationBufferShader_.getFragmentShaderObject()->addShaderExtension(
        "GL_ARB_shader_atomic_counter_ops", true);
    fillIllustrationBufferShader_.build();
    drawIllustrationBufferShader_.build();
    resolveNeighborsIllustrationBufferShader_.build();
    smoothIllustrationBufferShader_.build();
}

void FragmentListRenderer::initBuffers(const size2_t& screenSize) {
    if (screenSize != screenSize_ || abufferIdxImg_ == 0) {
        screenSize_ = screenSize;
        // delete screen size textures
        if (abufferIdxImg_) delete abufferIdxImg_;
        abufferIdxImg_ = 0;

        // reallocate screen size texture that holds the pointer to the end of the fragment list at
        // that pixel
        abufferIdxImg_ = new Texture2D(screenSize, GL_RED, GL_R32F, GL_FLOAT, GL_NEAREST, 0);
        abufferIdxImg_->bind();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, static_cast<GLsizei>(screenSize.x),
                     static_cast<GLsizei>(screenSize.y), 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        LGL_ERROR;

        LogInfo("fragment-list: screen size buffers allocated of size " << screenSize);
    }

    if (oldFragmentSize_ != fragmentSize_ || pixelBuffer_ == 0) {
        oldFragmentSize_ = fragmentSize_;
        // create new SSBO for the pixel storage
        if (pixelBuffer_) glDeleteBuffers(1, &pixelBuffer_);
        pixelBuffer_ = 0;
        glGenBuffers(1, &pixelBuffer_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, pixelBuffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, fragmentSize_ * 4 * sizeof(GLfloat), NULL,
                     GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        LGL_ERROR;

        LogInfo("fragment-list: pixel storage for "
                << fragmentSize_ << " pixels allocated, memory usage: "
                << (fragmentSize_ * 4 * sizeof(GLfloat) / 1024 / 1024.0f) << " MB");
    }
}

void FragmentListRenderer::assignUniforms(Shader& shader) const {
    // screen size textures
    glActiveTexture(abufferIdxUnit_->getEnum());
    abufferIdxImg_->bind();
    glBindImageTexture(abufferIdxUnit_->getUnitNumber(), abufferIdxImg_->getID(), 0, false, 0,
                       GL_READ_WRITE, GL_R32UI);
    LGL_ERROR;
    shader.setUniform("abufferIdxImg", abufferIdxUnit_->getUnitNumber());
    glActiveTexture(GL_TEXTURE0);
    LGL_ERROR;

    // pixel storage
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 6, atomicCounter_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, pixelBuffer_);
    LGL_ERROR;

    // other uniforms
    shader.setUniform("AbufferParams.screenWidth", static_cast<GLint>(screenSize_.x));
    shader.setUniform("AbufferParams.screenHeight", static_cast<GLint>(screenSize_.y));
    shader.setUniform("AbufferParams.storageSize", static_cast<GLuint>(fragmentSize_));
}

void FragmentListRenderer::drawQuad() const {
    utilgl::GlBoolState depthTest(GL_DEPTH_TEST, false);
    utilgl::DepthMaskState depthMask(GL_FALSE);
    utilgl::CullFaceState culling(GL_NONE);
    auto rect = SharedOpenGLResources::getPtr()->imagePlaneRect();
    utilgl::Enable<MeshGL> enable(rect);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void FragmentListRenderer::debugFragmentLists(GLuint numFrags) {
    std::ostringstream oss;
    oss << "========= Fragment List Renderer - DEBUG =========\n\n";

    // read global counter
    GLuint counter = 0xffffffff;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounter_);
    LGL_ERROR;
    glGetBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &counter);
    LGL_ERROR;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
    LGL_ERROR;
    oss << "FLR: value of the global counter: " << counter << std::endl;

    oss << "fragment query: " << numFrags << std::endl;
    oss << "global counter: " << counter << std::endl;

    // read index image
    oss << "Index image:" << std::endl;
    std::vector<GLuint> idxImg(screenSize_.x * screenSize_.y);
    abufferIdxImg_->download(&idxImg[0]);
    LGL_ERROR;
    for (size_t y = 0; y < screenSize_.y; ++y) {
        oss << "y = " << y;
        for (size_t x = 0; x < screenSize_.x; ++x) {
            oss << " " << idxImg[x + screenSize_.x * y];
        }
        oss << std::endl;
    }

    // read pixel storage buffer

    oss << std::endl << "Pixel storage: " << std::endl;
    glBindBuffer(GL_ARRAY_BUFFER, pixelBuffer_);
    LGL_ERROR;
    size_t size = std::min((size_t)counter, fragmentSize_);
    std::vector<GLfloat> pixelBuffer(4 * counter);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(GLfloat) * 4 * size, &pixelBuffer[0]);
    LGL_ERROR;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    LGL_ERROR;
    for (size_t i = 0; i < size; ++i) {
        GLuint previous = *reinterpret_cast<GLuint*>(&pixelBuffer[4 * i]);
        GLfloat depth = pixelBuffer[4 * i + 1];
        GLfloat alpha = pixelBuffer[4 * i + 2];
        GLuint c = *reinterpret_cast<GLuint*>(&pixelBuffer[4 * i + 3]);
        float r = float((c >> 20) & 0x3ff) / 1023.0f;
        float g = float((c >> 10) & 0x3ff) / 1023.0f;
        float b = float(c & 0x3ff) / 1023.0f;
        oss << fmt::format("%5d: previous=%5d, depth=%6.3f, alpha=%5.3f, r=%5.3f, g=%5.3f, b=%5.3f\n", i,
               (int)previous, (float)depth, (float)alpha, r, g, b);
    }

    oss << std::endl << "\n==================================================" << std::endl;
}

void FragmentListRenderer::initIllustrationBuffer() {
    if (illustrationBufferOldScreenSize_ != screenSize_) {
        illustrationBufferOldScreenSize_ = screenSize_;
        // reallocate textures with head and count

        if (illustrationBufferIdxImg_) delete illustrationBufferIdxImg_;
        if (illustrationBufferCountImg_) delete illustrationBufferCountImg_;

        // reallocate screen size texture that holds the pointer to the begin of the block of
        // fragments
        illustrationBufferIdxImg_ =
            new Texture2D(screenSize_, GL_RED, GL_R32F, GL_FLOAT, GL_NEAREST, 0);
        illustrationBufferIdxImg_->bind();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, static_cast<GLsizei>(screenSize_.x),
                     static_cast<GLsizei>(screenSize_.y), 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        LGL_ERROR;
        // reallocate screen size texture that holds the count of fragments at that pixel
        illustrationBufferCountImg_ =
            new Texture2D(screenSize_, GL_RED, GL_R32F, GL_FLOAT, GL_NEAREST, 0);
        illustrationBufferCountImg_->bind();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, static_cast<GLsizei>(screenSize_.x),
                     static_cast<GLsizei>(screenSize_.y), 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        LGL_ERROR;

        LogInfo("Illustration Buffers: additional screen size buffers allocated of size "
                << screenSize_);
    }

    if (illustrationBufferOldFragmentSize_ != fragmentSize_) {
        illustrationBufferOldFragmentSize_ = fragmentSize_;
        // reallocate SSBO for the illustration buffer storage
        // color: alpha+rgb
        if (illustrationColorBuffer_) glDeleteBuffers(1, &illustrationColorBuffer_);
        glGenBuffers(1, &illustrationColorBuffer_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, illustrationColorBuffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, fragmentSize_ * 2 * sizeof(GLfloat), NULL,
                     GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        LGL_ERROR;
        // surface info: depth, gradient, compressed normal (not yet)
        if (illustrationSurfaceInfoBuffer_) glDeleteBuffers(1, &illustrationSurfaceInfoBuffer_);
        glGenBuffers(1, &illustrationSurfaceInfoBuffer_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, illustrationSurfaceInfoBuffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, fragmentSize_ * 2 * sizeof(GLfloat), NULL,
                     GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        LGL_ERROR;
        // smoothing: beta + gamma
        if (illustrationSmoothingBuffer_[0]) glDeleteBuffers(2, illustrationSmoothingBuffer_);
        glGenBuffers(2, illustrationSmoothingBuffer_);
        for (int i = 0; i < 2; ++i) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, illustrationSmoothingBuffer_[i]);
            glBufferData(GL_SHADER_STORAGE_BUFFER, fragmentSize_ * 2 * sizeof(GLfloat), NULL,
                         GL_DYNAMIC_DRAW);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            LGL_ERROR;
        }
        // reuse fragment lists as neighborhood storage

        // log size
        int sizePerFragment = 6 * sizeof(GLfloat);
        LogInfo("Illustration Buffers: additional pixel storage for "
                << fragmentSize_ << " pixels allocated, memory usage: "
                << (fragmentSize_ * sizePerFragment / 1024 / 1024.0f) << " MB");
    }
}

void FragmentListRenderer::fillIllustrationBuffer() {
    // reset counter
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomicCounter_);
    LGL_ERROR;
    GLuint v[1] = {0};
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), v);
    LGL_ERROR;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
    LGL_ERROR;

    // create unit index for index textures
    illustrationBufferIdxUnit_ = new TextureUnit();
    illustrationBufferCountUnit_ = new TextureUnit();

    // execute sort+fill shader
    fillIllustrationBufferShader_.activate();
    assignUniforms(fillIllustrationBufferShader_);
    assignIllustrationBufferUniforms(fillIllustrationBufferShader_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, illustrationColorBuffer_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, illustrationSurfaceInfoBuffer_);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 6, atomicCounter_);
    drawQuad();
    fillIllustrationBufferShader_.deactivate();
}

void FragmentListRenderer::processIllustrationBuffer() {
    // resolve neighbors
    // and set initial conditions for silhouettes+halos
    resolveNeighborsIllustrationBufferShader_.activate();
    assignIllustrationBufferUniforms(resolveNeighborsIllustrationBufferShader_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
                     illustrationSurfaceInfoBuffer_);             // in: depth+gradient
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, pixelBuffer_);  // out: neighbors
    glBindBufferBase(
        GL_SHADER_STORAGE_BUFFER, 2,
        illustrationSmoothingBuffer_[1 - activeIllustrationSmoothingBuffer_]);  // out: beta+gamma
    activeIllustrationSmoothingBuffer_ = 1 - activeIllustrationSmoothingBuffer_;
    drawQuad();
    resolveNeighborsIllustrationBufferShader_.deactivate();

    // perform the bluring
    if (illustrationBufferSettings_.smoothingSteps_ > 0) {
        smoothIllustrationBufferShader_.activate();
        smoothIllustrationBufferShader_.setUniform(
            "lambdaBeta", float(1) - illustrationBufferSettings_.edgeSmoothing_);
        smoothIllustrationBufferShader_.setUniform(
            "lambdaGamma", float(1) - illustrationBufferSettings_.haloSmoothing_);
        for (int i = 0; i < illustrationBufferSettings_.smoothingSteps_; ++i) {
            assignIllustrationBufferUniforms(smoothIllustrationBufferShader_);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, pixelBuffer_);  // in: neighbors
            glBindBufferBase(
                GL_SHADER_STORAGE_BUFFER, 1,
                illustrationSmoothingBuffer_[activeIllustrationSmoothingBuffer_]);  // in:
                                                                                    // beta+gamma
            glBindBufferBase(
                GL_SHADER_STORAGE_BUFFER, 2,
                illustrationSmoothingBuffer_[1 -
                                             activeIllustrationSmoothingBuffer_]);  // out:
                                                                                    // beta+gamma
            activeIllustrationSmoothingBuffer_ = 1 - activeIllustrationSmoothingBuffer_;
            drawQuad();
        }
        smoothIllustrationBufferShader_.deactivate();
    }
}

void FragmentListRenderer::drawIllustrationBuffer() {
    // final blending
    drawIllustrationBufferShader_.activate();
    assignIllustrationBufferUniforms(drawIllustrationBufferShader_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, illustrationColorBuffer_);
    glBindBufferBase(
        GL_SHADER_STORAGE_BUFFER, 1,
        illustrationSmoothingBuffer_[activeIllustrationSmoothingBuffer_]);  // in: beta+gamma
    vec4 edgeColor =
        vec4(illustrationBufferSettings_.edgeColor_, illustrationBufferSettings_.edgeStrength_);
    drawIllustrationBufferShader_.setUniform("edgeColor", edgeColor);
    drawIllustrationBufferShader_.setUniform("haloStrength",
                                             illustrationBufferSettings_.haloStrength_);
    drawQuad();
    drawIllustrationBufferShader_.deactivate();

    // free unit for index textures
    delete illustrationBufferIdxUnit_;
    illustrationBufferIdxUnit_ = nullptr;
    delete illustrationBufferCountUnit_;
    illustrationBufferCountUnit_ = nullptr;
}

void FragmentListRenderer::assignIllustrationBufferUniforms(Shader& shader) {
    // screen size textures
    glActiveTexture(illustrationBufferIdxUnit_->getEnum());
    illustrationBufferIdxImg_->bind();
    glBindImageTexture(illustrationBufferIdxUnit_->getUnitNumber(),
                       illustrationBufferIdxImg_->getID(), 0, false, 0, GL_READ_WRITE, GL_R32UI);
    LGL_ERROR;
    shader.setUniform("illustrationBufferIdxImg", illustrationBufferIdxUnit_->getUnitNumber());
    glActiveTexture(GL_TEXTURE0);
    LGL_ERROR;

    glActiveTexture(illustrationBufferCountUnit_->getEnum());
    illustrationBufferCountImg_->bind();
    glBindImageTexture(illustrationBufferCountUnit_->getUnitNumber(),
                       illustrationBufferCountImg_->getID(), 0, false, 0, GL_READ_WRITE, GL_R32UI);
    LGL_ERROR;
    shader.setUniform("illustrationBufferCountImg", illustrationBufferCountUnit_->getUnitNumber());
    glActiveTexture(GL_TEXTURE0);
    LGL_ERROR;

    // other uniforms
    shader.setUniform("screenSize", ivec2(screenSize_.x, screenSize_.y));
}

void FragmentListRenderer::debugIllustrationBuffer(GLuint numFrags) {
    printf("========= Fragment List Renderer - DEBUG Illustration Buffers =========\n\n");

    // read images
    std::vector<GLuint> idxImg(screenSize_.x * screenSize_.y);
    illustrationBufferIdxImg_->download(&idxImg[0]);
    LGL_ERROR;
    std::vector<GLuint> countImg(screenSize_.x * screenSize_.y);
    illustrationBufferCountImg_->download(&countImg[0]);
    LGL_ERROR;

    // read pixel storage buffer
    size_t size = std::min((size_t)numFrags, fragmentSize_);

    glBindBuffer(GL_ARRAY_BUFFER, illustrationColorBuffer_);
    LGL_ERROR;
    std::vector<glm::tvec2<GLfloat>> colorBuffer(size);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 2 * sizeof(GLfloat) * size, &colorBuffer[0]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    LGL_ERROR;

    glBindBuffer(GL_ARRAY_BUFFER, illustrationSurfaceInfoBuffer_);
    LGL_ERROR;
    std::vector<glm::tvec2<GLfloat>> surfaceInfoBuffer(size);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 2 * sizeof(GLfloat) * size, &surfaceInfoBuffer[0]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    LGL_ERROR;

    glBindBuffer(GL_ARRAY_BUFFER, pixelBuffer_);
    LGL_ERROR;
    std::vector<glm::tvec4<GLint>> neighborBuffer(size);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 4 * sizeof(GLint) * size, &neighborBuffer[0]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    LGL_ERROR;

    glBindBuffer(GL_ARRAY_BUFFER,
                 illustrationSmoothingBuffer_[1 - activeIllustrationSmoothingBuffer_]);
    LGL_ERROR;
    std::vector<glm::tvec2<GLfloat>> smoothingBuffer(size);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, 2 * sizeof(GLfloat) * size, &smoothingBuffer[0]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    LGL_ERROR;

    // print
    for (size_t y = 0; y < screenSize_.y; ++y) {
        for (size_t x = 0; x < screenSize_.x; ++x) {
            auto start = idxImg[x + screenSize_.x * y];
            auto count = countImg[x + screenSize_.x * y];
            printf(" %4d:%4d:  start=%5d, count=%5d\n", (int)x, (int)y, start, count);
            for (uint32_t i = 0; i < count; ++i) {
                float alpha = colorBuffer[start + i].x;
                int rgb = *reinterpret_cast<int*>(&colorBuffer[start + i].y);
                float depth = surfaceInfoBuffer[start + i].x;
                glm::tvec4<GLint> neighbors = neighborBuffer[start + i];
                float beta = smoothingBuffer[start + i].x;
                float gamma = smoothingBuffer[start + i].y;
                float r = float((rgb >> 20) & 0x3ff) / 1023.0f;
                float g = float((rgb >> 10) & 0x3ff) / 1023.0f;
                float b = float(rgb & 0x3ff) / 1023.0f;
                printf(
                    "     depth=%5.3f, alpha=%5.3f, r=%5.3f, g=%5.3f, b=%5.3f, beta=%5.3f, "
                    "gamma=%5.3f, neighbors:",
                    depth, alpha, r, g, b, beta, gamma);
                if (neighbors.x >= 0) {
                    if (neighbors.x < size) {
                        printf("(%d:%5.3f)", neighbors.x, surfaceInfoBuffer[neighbors.x].x);
                    } else {

                        printf("(>size)");
                    }
                } else {
                    printf("(-1)");
                }
                if (neighbors.y >= 0) {
                    if (neighbors.y < size) {
                        printf("(%d:%5.3f)", neighbors.y, surfaceInfoBuffer[neighbors.y].x);
                    } else {
                        printf("(>size)");
                    }
                } else
                    printf("(-1)");
                if (neighbors.z >= 0) {
                    if (neighbors.z < size)
                        printf("(%d:%5.3f)", neighbors.z, surfaceInfoBuffer[neighbors.z].x);
                    else
                        printf("(>size)");
                } else
                    printf("(-1)");
                if (neighbors.w >= 0) {
                    if (neighbors.w < size)
                        printf("(%d:%5.3f)", neighbors.w, surfaceInfoBuffer[neighbors.w].x);
                    else
                        printf("(>size)");
                } else
                    printf("(-1)");
                printf("\n");
            }
        }
    }

    printf("\n==================================================\n");
}

}  // namespace inviwo
