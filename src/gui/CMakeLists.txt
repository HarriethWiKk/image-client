# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 HarriethWiKk
qt_add_executable(ImageClient WIN32 main.cpp)

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

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

target_link_libraries(ImageClient PRIVATE
    oic-core
    oic-app
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
