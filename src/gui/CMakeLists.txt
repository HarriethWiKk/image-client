# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 HarriethWiKk
qt_add_executable(ImageClient WIN32 main.cpp)

# Must precede qt_add_qml_module: CMake reads this property while generating the
# module's qmldir. Set after the call, Theme is emitted as a plain type and every
# `Theme.*` binding resolves against the type name -> undefined.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(ImageClient
    URI App
    VERSION 1.0
    QML_FILES
        qml/Theme.qml
        qml/AppCard.qml
        qml/AppButton.qml
        qml/AppIconButton.qml
        qml/AppTextField.qml
        qml/AppTextArea.qml
        qml/AppComboBox.qml
        qml/AppStatusBadge.qml
        qml/AppToast.qml
        qml/Main.qml
        qml/LightboxOverlay.qml
)

target_link_libraries(ImageClient PRIVATE
    oic-core
    oic-app
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
