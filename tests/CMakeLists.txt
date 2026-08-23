# Unit tests read recorded Emby JSON from the source tree — no network (AGENTS.md).
set(STRMQT_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mocks/emby")

function(strmqt_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE strmqt_app strmqt_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE STRMQT_FIXTURES_DIR="${STRMQT_FIXTURES_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

strmqt_add_test(tst_settings unit/tst_settings.cpp)
strmqt_add_test(tst_emby_dto unit/tst_emby_dto.cpp)
strmqt_add_test(tst_secrets_store unit/tst_secrets_store.cpp)

strmqt_add_test(tst_models unit/tst_models.cpp)
strmqt_add_test(tst_playback_ticket unit/tst_playback_ticket.cpp)
strmqt_add_test(tst_media_source unit/tst_media_source.cpp)
strmqt_add_test(tst_input_map unit/tst_input_map.cpp)
strmqt_add_test(tst_stick_decision unit/tst_stick_decision.cpp)
strmqt_add_test(tst_gamepad_decision unit/tst_gamepad_decision.cpp)
strmqt_add_test(tst_item_details unit/tst_item_details.cpp)
strmqt_add_test(tst_settings_prefs unit/tst_settings_prefs.cpp)

# Integration: client/controllers against a local mock HTTP server replaying fixtures.
strmqt_add_test(tst_emby_client
    integration/tst_emby_client.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
)
target_include_directories(tst_emby_client PRIVATE mocks)

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
