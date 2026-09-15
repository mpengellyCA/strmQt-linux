#pragma once

#include <QString>
#include <vector>

namespace strmqt {

// Standalone QR Code SVG generator (Nayuki algorithm).
// Produces an SVG XML string suitable for rendering in QML or a web browser.
class QrCodeGenerator
{
public:
    enum class Ecc { Low, Medium, Quartile, High };

    // Returns an SVG string of the QR code with given content and margin.
    static QString toSvg(const QString &text, int border = 4,
                        const QString &foreground = QStringLiteral("#0C0B0A"),
                        const QString &background = QStringLiteral("#FFFFFF"));

    // Generates a boolean 2D grid of modules (true = dark, false = light).
    static std::vector<std::vector<bool>> encodeText(const QString &text, Ecc ecc = Ecc::Medium);
};

} // namespace strmqt
