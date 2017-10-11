/*********************************************************************************
 *
 * Inviwo - Interactive Visualization Workshop
 *
 * Copyright (c) 2017 Inviwo Foundation
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

#ifndef IVW_FONTSIZEPROPERTYWIDGETQT_H
#define IVW_FONTSIZEPROPERTYWIDGETQT_H

#include <modules/qtwidgets/properties/propertywidgetqt.h>

#include <inviwo/core/properties/ordinalproperty.h>

#include <array>

namespace inviwo {

class IvwComboBox;
class EditableLabelQt;

class IVW_MODULE_QTWIDGETS_API FontSizePropertyWidgetQt : public PropertyWidgetQt {
public:
    FontSizePropertyWidgetQt(IntProperty* property);
    virtual void updateFromProperty() override;

private: 
    IntProperty* property_;
    IvwComboBox* comboBox_;
    EditableLabelQt* label_;

    const std::array<int, 15> fontSizes_ = { {6, 8, 10, 11, 12, 14, 16, 20, 24, 28, 36, 48, 60, 72, 96} };
};

} // namespace

#endif  // IVW_FONTSIZEPROPERTYWIDGETQT_H