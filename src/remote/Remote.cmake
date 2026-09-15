qt_add_library(strmqt_remote STATIC
    remote/NetworkAddressHelper.h remote/NetworkAddressHelper.cpp
    remote/TlsCertificateGenerator.h remote/TlsCertificateGenerator.cpp
    remote/QrCodeGenerator.h remote/QrCodeGenerator.cpp
    remote/WebRemoteServer.h remote/WebRemoteServer.cpp
    remote/WebRemoteController.h remote/WebRemoteController.cpp
)

qt_add_resources(strmqt_remote "strmqt_webremote_assets"
    PREFIX "/webremote"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}/remote/web
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/remote/web/index.html
        ${CMAKE_CURRENT_SOURCE_DIR}/remote/web/style.css
        ${CMAKE_CURRENT_SOURCE_DIR}/remote/web/app.js
        ${CMAKE_CURRENT_SOURCE_DIR}/remote/web/manifest.json
        ${CMAKE_CURRENT_SOURCE_DIR}/remote/web/icon.svg
)

target_include_directories(strmqt_remote PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(strmqt_remote PUBLIC
    strmqt_app
    strmqt_core
    Qt6::Network
    OpenSSL::Crypto
)
