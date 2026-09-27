# Tier shims (spec 2026-09-27 §4.3). Exactly one directory joins the module;
# the type name is the file's basename, so both tiers register the same types
# and callers never import an effects module themselves.
set(_strmqt_shims StrmTint StrmShadow StrmMask StrmBackdropBlur
    TabularText TabularMetrics CrateDisplayText)
list(TRANSFORM _strmqt_shims PREPEND "ui/shims/${STRMQT_QML_TIER_RESOLVED}/")
list(TRANSFORM _strmqt_shims APPEND ".qml")
qt_target_qml_sources(strmqt QML_FILES ${_strmqt_shims})
