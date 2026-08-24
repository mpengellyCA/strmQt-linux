#pragma once

#include <memory>

namespace strmqt::emby {

class EmbyClient;

// Owns cancellation for one EmbyClient request. Handles are move-only so one
// controller lane has one clear owner. Destroying or reusing a handle aborts
// its unfinished reply; the request's QFuture still resolves with a canceled
// Result rather than being abandoned.
class RequestHandle
{
public:
    RequestHandle() = default;
    ~RequestHandle();

    RequestHandle(const RequestHandle &) = delete;
    RequestHandle &operator=(const RequestHandle &) = delete;
    RequestHandle(RequestHandle &&other) noexcept;
    RequestHandle &operator=(RequestHandle &&other) noexcept;

    void cancel();
    bool active() const;

private:
    struct State;
    std::shared_ptr<State> m_state;

    friend class EmbyClient;
};

} // namespace strmqt::emby
