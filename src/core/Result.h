#pragma once

#include <QString>

#include <utility>

namespace strmqt {

// Value-or-error carrier for async operations (no exceptions across QFuture).
// An empty error string means success.
template<class T> struct Result
{
    T value{};
    QString error;

    bool ok() const { return error.isEmpty(); }

    static Result success(T v) { return Result{std::move(v), QString()}; }
    static Result failure(QString message)
    {
        // Guarantee ok() == false even for callers passing an empty message.
        if (message.isEmpty())
            message = QStringLiteral("unknown error");
        return Result{T{}, std::move(message)};
    }
};

} // namespace strmqt
