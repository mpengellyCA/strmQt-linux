# Unit tests read recorded Emby JSON from the source tree — no network (AGENTS.md).
set(STRMQT_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mocks/emby")

function(strmqt_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE strmqt_app strmqt_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE STRMQT_FIXTURES_DIR="${STRMQT_FIXTURES_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

strmqt_add_test(tst_settings unit/tst_settings.cpp)
strmqt_add_test(tst_log_redaction unit/tst_log_redaction.cpp)
strmqt_add_test(tst_emby_dto unit/tst_emby_dto.cpp)
strmqt_add_test(tst_secrets_store unit/tst_secrets_store.cpp)

strmqt_add_test(tst_models unit/tst_models.cpp)
strmqt_add_test(tst_search_sections unit/tst_search_sections.cpp)
strmqt_add_test(tst_home_rails unit/tst_home_rails.cpp)
target_link_libraries(tst_home_rails PRIVATE Qt6::Gui Qt6::Quick)
set_tests_properties(tst_home_rails PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
strmqt_add_test(tst_playback_ticket unit/tst_playback_ticket.cpp)
strmqt_add_test(tst_media_source unit/tst_media_source.cpp)
strmqt_add_test(tst_input_map unit/tst_input_map.cpp)
strmqt_add_test(tst_stick_decision unit/tst_stick_decision.cpp)
strmqt_add_test(tst_gamepad_decision unit/tst_gamepad_decision.cpp)
strmqt_add_test(tst_vlc_frame_buffers unit/tst_vlc_frame_buffers.cpp)
target_link_libraries(tst_vlc_frame_buffers PRIVATE Qt6::Gui)
strmqt_add_test(tst_application_policy unit/tst_application_policy.cpp)
strmqt_add_test(tst_power_inhibit unit/tst_power_inhibit.cpp)
strmqt_add_test(tst_image_limits unit/tst_image_limits.cpp)
target_link_libraries(tst_image_limits PRIVATE Qt6::Gui)
strmqt_add_test(tst_item_details unit/tst_item_details.cpp)
strmqt_add_test(tst_settings_prefs unit/tst_settings_prefs.cpp)

strmqt_add_test(tst_qml_accessibility unit/tst_qml_accessibility.cpp)
target_link_libraries(tst_qml_accessibility PRIVATE Qt6::Gui Qt6::Quick)
target_compile_definitions(tst_qml_accessibility PRIVATE STRMQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_tests_properties(tst_qml_accessibility PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1")

strmqt_add_test(tst_navigation_history unit/tst_navigation_history.cpp)
target_link_libraries(tst_navigation_history PRIVATE Qt6::Gui Qt6::Quick)
target_compile_definitions(tst_navigation_history PRIVATE STRMQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_tests_properties(tst_navigation_history PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

strmqt_add_test(tst_mpv_video_item unit/tst_mpv_video_item.cpp
    ${CMAKE_SOURCE_DIR}/src/playback/mpv/MpvPlayer.cpp
    ${CMAKE_SOURCE_DIR}/src/playback/mpv/MpvVideoItem.cpp
)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV_RENDER_TEST REQUIRED IMPORTED_TARGET mpv)
target_link_libraries(tst_mpv_video_item PRIVATE
    Qt6::Gui Qt6::Quick Qt6::OpenGL PkgConfig::MPV_RENDER_TEST)
set_tests_properties(tst_mpv_video_item PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Integration: client/controllers against a local mock HTTP server replaying fixtures.
strmqt_add_test(tst_emby_client
    integration/tst_emby_client.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
)
target_include_directories(tst_emby_client PRIVATE mocks)

# EmbyImageProvider lives in the executable rather than strmqt_app (it needs
# QtGui), so it is compiled into this target the same way CoverTint.cpp is.
strmqt_add_test(tst_image_cache
    integration/tst_image_cache.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
    ${CMAKE_SOURCE_DIR}/src/app/EmbyImageProvider.cpp
)
target_include_directories(tst_image_cache PRIVATE mocks)
target_link_libraries(tst_image_cache PRIVATE Qt6::Gui Qt6::Quick)
target_compile_definitions(tst_image_cache PRIVATE STRMQT_IMAGE_CACHE_TESTS)
set_tests_properties(tst_image_cache PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

strmqt_add_test(tst_controllers
    integration/tst_controllers.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
)
target_include_directories(tst_controllers PRIVATE mocks)

strmqt_add_test(tst_library_query
    integration/tst_library_query.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
)
target_include_directories(tst_library_query PRIVATE mocks)

strmqt_add_test(tst_music_query
    integration/tst_music_query.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
)
target_include_directories(tst_music_query PRIVATE mocks)

strmqt_add_test(tst_content_controllers
    integration/tst_content_controllers.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
)
target_include_directories(tst_content_controllers PRIVATE mocks)

strmqt_add_test(tst_player_controller
    integration/tst_player_controller.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
    mocks/FakePlayerBackend.h
)
target_include_directories(tst_player_controller PRIVATE mocks)

strmqt_add_test(tst_item_actions
    integration/tst_item_actions.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
    mocks/FakePlayerBackend.h
)
target_include_directories(tst_item_actions PRIVATE mocks)

# FakePlayerBackend.h must be listed: it carries Q_OBJECT and AUTOMOC will not
# generate its metaobject from an include alone (undefined vtable at link).
strmqt_add_test(tst_player_tracks unit/tst_player_tracks.cpp mocks/FakePlayerBackend.h)
target_include_directories(tst_player_tracks PRIVATE mocks)

strmqt_add_test(tst_play_queue unit/tst_play_queue.cpp)

# The one Qt semantic TrackRow's multi-select rests on, probed live: a pointer
# handler's own `point` carries keyboard modifiers and the `tapped` signal's
# QEventPoint does not. Needs QtQuick and a window, so it runs offscreen — there
# is no display in CI and none reachable from an agent here either.
strmqt_add_test(tst_tap_modifiers unit/tst_tap_modifiers.cpp)
target_link_libraries(tst_tap_modifiers PRIVATE Qt6::Quick)
set_tests_properties(tst_tap_modifiers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The other Qt semantic a control rests on, probed the same way: where a
# window-scoped Shortcut sits relative to a focused text field. MappedShortcut's
# two-Shortcut split is only correct if a chord gets past a field and a bare
# typable key does not — and "Space" is the one that was on the wrong side.
strmqt_add_test(tst_shortcut_typing unit/tst_shortcut_typing.cpp)
target_link_libraries(tst_shortcut_typing PRIVATE Qt6::Quick)
set_tests_properties(tst_shortcut_typing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The wash tint is QtGui (QImage/QColor) and so cannot live in strmqt_app, which
# is deliberately QtCore-only; the unit is compiled straight into the test. The
# clamp is pure — an image in, a colour out — but the cache around it is driven
# by EmbyImageFetcher's decode signal, so the fetcher comes along to be the thing
# that emits it. It is constructed with a null client and never fetches: no
# network in a unit test (AGENTS.md).
strmqt_add_test(tst_cover_tint unit/tst_cover_tint.cpp
    ${CMAKE_SOURCE_DIR}/src/app/CoverTint.cpp
    ${CMAKE_SOURCE_DIR}/src/app/CoverTintService.cpp
    ${CMAKE_SOURCE_DIR}/src/app/EmbyImageProvider.cpp
)
target_link_libraries(tst_cover_tint PRIVATE Qt6::Gui Qt6::Quick)

# MprisPlayer assembles Metadata without a registered D-Bus connection, so the
# map and its value types are testable on a machine with no session bus.
strmqt_add_test(tst_mpris_metadata unit/tst_mpris_metadata.cpp)

strmqt_add_test(tst_queue_playback
    integration/tst_queue_playback.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
    mocks/FakePlayerBackend.h
)
target_include_directories(tst_queue_playback PRIVATE mocks)

strmqt_add_test(tst_item_actions_queue
    integration/tst_item_actions_queue.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
    mocks/FakePlayerBackend.h
)
target_include_directories(tst_item_actions_queue PRIVATE mocks)

strmqt_add_test(tst_emby_websocket unit/tst_emby_websocket.cpp)

strmqt_add_test(tst_remote_control
    integration/tst_remote_control.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
    mocks/FakePlayerBackend.h
)
target_include_directories(tst_remote_control PRIVATE mocks)

strmqt_add_test(tst_live_updates
    integration/tst_live_updates.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
)
target_include_directories(tst_live_updates PRIVATE mocks)
