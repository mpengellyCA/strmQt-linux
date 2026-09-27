# QML tier selection (spec 2026-09-27 §3). One build decision: which directory
# of src/ui/shims joins the StrmQt module. Never a runtime check.
set(STRMQT_QML_TIER "auto" CACHE STRING
    "QML tier: auto (full on Qt >= 6.8), full (needs Qt >= 6.7), or compat")
set_property(CACHE STRMQT_QML_TIER PROPERTY STRINGS auto full compat)

if(STRMQT_QML_TIER STREQUAL "auto")
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
        set(STRMQT_QML_TIER_RESOLVED full)
    else()
        set(STRMQT_QML_TIER_RESOLVED compat)
    endif()
elseif(STRMQT_QML_TIER STREQUAL "full")
    if(Qt6_VERSION VERSION_LESS 6.7)
        message(FATAL_ERROR "STRMQT_QML_TIER=full needs Qt >= 6.7 (font.variableAxes); "
                            "found ${Qt6_VERSION}. Use auto or compat.")
    endif()
    set(STRMQT_QML_TIER_RESOLVED full)
elseif(STRMQT_QML_TIER STREQUAL "compat")
    set(STRMQT_QML_TIER_RESOLVED compat)
else()
    message(FATAL_ERROR "STRMQT_QML_TIER must be auto, full or compat (got '${STRMQT_QML_TIER}')")
endif()

message(STATUS "StrmQt QML tier: ${STRMQT_QML_TIER_RESOLVED} (Qt ${Qt6_VERSION})")
file(WRITE "${CMAKE_BINARY_DIR}/strmqt-qml-tier.txt" "${STRMQT_QML_TIER_RESOLVED}\n")
