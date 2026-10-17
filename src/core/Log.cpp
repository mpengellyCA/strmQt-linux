#include "Log.h"

#include <algorithm>
#include <cstdio>

#include <QRegularExpression>
#include <QVarLengthArray>

Q_LOGGING_CATEGORY(logApp, "strmqt.app")
Q_LOGGING_CATEGORY(logCore, "strmqt.core")
Q_LOGGING_CATEGORY(logServer, "strmqt.server")
Q_LOGGING_CATEGORY(logPlayback, "strmqt.playback")
Q_LOGGING_CATEGORY(logKeys, "strmqt.input.keys", QtInfoMsg)

using namespace strmqt;

namespace {

// ── Credential redaction ────────────────────────────────────────────────────
// A key regex finds where a credential starts; the functions below decide where
// it ends. That end is the whole problem. Stop late and the redaction eats the
// rest of the line (`QUrl("…?api_key=X")` losing its `")`); stop early and the
// tail of the secret is printed. The one rule that settles every doubtful case:
// a character ends a value only when something earlier on the line says it is
// punctuation — otherwise it belongs to the secret.

constexpr QLatin1StringView kRedacted("<redacted>");

// Names that carry a credential, matched as the END of an identifier:
// `AccessToken`, `refresh_token` and `NewPw` are credentials too, and an
// unrecognised compound is redacted rather than printed. The exceptions are
// the cursors and handles that merely happen to be called tokens.
#define STRMQT_CREDENTIAL_NAMES                                                                    \
    "api(?:_|%5f)key|access(?:_|%5f)token|x(?:-|%2d)emby(?:-|%2d)token"                            \
    "|(?<!cancel)(?<!cancellation)(?<!page)(?<!continuation)token"                                 \
    "|authorization|pw|password"

// The three ways a log line quotes a string: plainly, the same thing inside a
// string qDebug() already quoted (so every quote arrives as \"), and 'like this'.
enum class Quote { None, Plain, Escaped, Apostrophe };

Quote quoteAt(QStringView text, qsizetype at)
{
    if (at >= text.size())
        return Quote::None;
    if (text[at] == u'"')
        return Quote::Plain;
    if (text[at] == u'\'')
        return Quote::Apostrophe;
    if (text[at] == u'\\' && at + 1 < text.size() && text[at + 1] == u'"')
        return Quote::Escaped;
    return Quote::None;
}

qsizetype quoteWidth(Quote quote)
{
    return quote == Quote::None ? 0 : quote == Quote::Escaped ? 2 : 1;
}

char16_t bracketOpening(QChar closer)
{
    switch (closer.unicode()) {
    case u')':
        return u'(';
    case u']':
        return u'[';
    case u'}':
        return u'{';
    default:
        return 0;
    }
}

// Quotes and brackets opened on the line and not yet closed, innermost last.
class Enclosures
{
public:
    void clear() { m_open.clear(); }

    // Reads on through more of the line.
    void scan(QStringView line)
    {
        for (qsizetype i = 0; i < line.size(); ++i) {
            const QChar c = line[i];
            const Quote quote = quoteAt(line, i);
            if (quote == Quote::Apostrophe && !isOpen(quote) && i > 0
                && line[i - 1].isLetterOrNumber()) {
                continue; // the apostrophe in "couldn't" quotes nothing
            }
            if (quote != Quote::None) {
                if (!closeThrough(tag(quote)))
                    open(tag(quote));
                i += quoteWidth(quote) - 1;
            } else if (c == u'\\') {
                ++i; // some other escape: its next character is not syntax
            } else if (c == u'(' || c == u'[' || c == u'{') {
                open(c.unicode());
            } else if (const char16_t opening = bracketOpening(c)) {
                closeThrough(opening);
            }
        }
    }

    bool isOpen(Quote quote) const { return m_open.contains(tag(quote)); }

    // The brackets opened inside `quote` — or, for Quote::None, inside the
    // innermost quote still open. Only these can close before that quote does.
    QVarLengthArray<char16_t, 8> bracketsWithin(Quote quote) const
    {
        qsizetype from = m_open.size();
        while (from > 0 && (quote == Quote::None ? !isQuoteTag(m_open[from - 1])
                                                 : m_open[from - 1] != tag(quote))) {
            --from;
        }
        QVarLengthArray<char16_t, 8> brackets;
        for (qsizetype i = from; i < m_open.size(); ++i) {
            if (!isQuoteTag(m_open[i]))
                brackets.append(m_open[i]);
        }
        return brackets;
    }

private:
    static char16_t tag(Quote quote)
    {
        switch (quote) {
        case Quote::Plain:
            return u'"';
        case Quote::Escaped:
            return u'\\';
        case Quote::Apostrophe:
            return u'\'';
        case Quote::None:
            break;
        }
        return 0;
    }
    static bool isQuoteTag(char16_t entry)
    {
        return entry == u'"' || entry == u'\\' || entry == u'\'';
    }
    // Closing something also closes whatever was left open inside it.
    bool closeThrough(char16_t opening)
    {
        const qsizetype at = m_open.lastIndexOf(opening);
        if (at < 0)
            return false;
        m_open.resize(at);
        return true;
    }

    // Nothing a person reads nests this deep; a line that does is noise, and
    // the cap keeps every lookup above a short walk whatever the line holds.
    void open(char16_t opening)
    {
        if (m_open.size() < kMaxDepth)
            m_open.append(opening);
    }

    static constexpr qsizetype kMaxDepth = 16;
    QVarLengthArray<char16_t, kMaxDepth> m_open;
};

qsizetype lineStart(QStringView text, qsizetype at)
{
    return at > 0 ? text.lastIndexOf(u'\n', at - 1) + 1 : 0;
}

qsizetype lineEnd(QStringView text, qsizetype at)
{
    const qsizetype newline = text.indexOf(u'\n', at);
    return newline < 0 ? text.size() : newline;
}

// Index of the quote that closes a string whose content starts at `from`, or
// -1 when the line ends first. Escaped quotes inside the string do not close it.
qsizetype closingQuote(QStringView text, qsizetype from, Quote quote)
{
    const qsizetype end = lineEnd(text, from);
    qsizetype i = from;
    while (i < end) {
        const QChar c = text[i];
        if (quote == Quote::Apostrophe) {
            if (c == u'\'')
                return i;
            ++i;
        } else if (quote == Quote::Plain) {
            if (c == u'"')
                return i;
            i += c == u'\\' ? 2 : 1;
        } else if (c != u'\\' || i + 1 >= end) {
            ++i;
        } else if (text[i + 1] == u'"') {
            return i;
        } else if (text[i + 1] == u'\\') {
            // An escaped backslash is the string's own escape character, so
            // whatever follows it — \" and \\ included — is content.
            i += 2;
            i += (i + 1 < end && text[i] == u'\\') ? 2 : 1;
        } else {
            i += 2;
        }
    }
    return -1;
}

// Whether the text at `at` reads as what comes after a finished value, rather
// than as more of it.
bool mayFollowValue(QStringView text, qsizetype at)
{
    if (at >= text.size())
        return true;
    const QChar c = text[at];
    return c.isSpace() || c == u'&' || c == u',' || c == u';' || bracketOpening(c) != 0
           || quoteAt(text, at) != Quote::None;
}

// End of an unquoted value starting at `from`: the parameter separator or
// whitespace that has always ended one, or the closing half of something the
// line opened before the value began.
qsizetype bareValueEnd(QStringView text, qsizetype from, bool percentEncoded,
                       const Enclosures &open)
{
    // Whatever writes a "quoted" string escapes or percent-encodes the quotes
    // inside it, so the first bare one that closes an open string ends the
    // value — provided what follows could follow a string; a hand-formatted
    // line owes nobody that courtesy. Nothing escapes an apostrophe, and a URL
    // may hold one, so there only the last candidate is trusted.
    Quote closedBy = Quote::None;
    qsizetype lastApostrophe = -1;
    qsizetype end = from;
    for (; end < text.size(); ++end) {
        const QChar c = text[end];
        if (c.isSpace() || c == u'&')
            break;
        // %26 separates parameters only where the "=" was encoded as well; in
        // a plain query string it is an ampersand inside the value.
        if (percentEncoded && c == u'%' && text.sliced(end).startsWith(QLatin1StringView("%26")))
            break;
        const Quote quote = quoteAt(text, end);
        if (quote == Quote::Apostrophe && open.isOpen(quote)) {
            lastApostrophe = end;
        } else if (quote != Quote::None && open.isOpen(quote)
                   && mayFollowValue(text, end + quoteWidth(quote))) {
            closedBy = quote;
            break;
        }
        if (quote == Quote::Escaped)
            ++end; // not the end of anything: step over both characters
    }
    // …and then only when nothing but punctuation stands between it and the
    // end: "'https://…?api_key=X': timed out".
    const auto isPunctuation = [](QChar c) {
        return bracketOpening(c) != 0 || QStringView(u",;:.\"\\").contains(c);
    };
    if (lastApostrophe >= 0
        && std::all_of(text.cbegin() + lastApostrophe + 1, text.cbegin() + end, isPunctuation)) {
        closedBy = Quote::Apostrophe;
        end = lastApostrophe;
    }

    // Brackets can appear raw in a URL — and in a password — so one is read as
    // punctuation only at the very end of the value, where it closes a bracket
    // the line really opened: "(https://…?api_key=X), retrying".
    const auto isTrailing = [](QChar c) {
        return bracketOpening(c) != 0 || c == u',' || c == u';' || c == u':' || c == u'.';
    };
    const QVarLengthArray<char16_t, 8> brackets = open.bracketsWithin(closedBy);
    qsizetype trailing = end;
    while (trailing > from && isTrailing(text[trailing - 1]))
        --trailing;
    for (qsizetype start = trailing; start < end; ++start) {
        if (bracketOpening(text[start]) == 0)
            continue;
        qsizetype depth = brackets.size();
        bool balanced = true;
        for (qsizetype i = start; i < end && balanced; ++i) {
            const char16_t opening = bracketOpening(text[i]);
            if (opening == 0)
                continue;
            balanced = depth > 0 && brackets[depth - 1] == opening;
            --depth;
        }
        if (balanced)
            return start;
    }
    return end;
}

// Appends the redacted form of the value starting at `from` and returns where
// the value ended. `open` is what the line had opened before the value's key.
qsizetype redactValue(QStringView text, qsizetype from, bool percentEncoded,
                      const Enclosures &open, QString &out)
{
    const qsizetype bareEnd = bareValueEnd(text, from, percentEncoded, open);

    // key="value": the quotes are the value's own unless they close a string
    // the key itself sits in (`"…?api_key="` is an empty value).
    const Quote quote = quoteAt(text, from);
    if (quote != Quote::None && !open.isOpen(quote)) {
        const qsizetype width = quoteWidth(quote);
        const qsizetype close = closingQuote(text, from + width, quote);
        if (close < 0) {
            // Never closed — a truncated line. All of it may be the secret.
            out += text.sliced(from, width);
            out += kRedacted;
            return lineEnd(text, from);
        }
        const qsizetype after = close + width;
        // Text running on past the closing quote means this was no quoted
        // value, only a secret that starts with a quote: redact it the long way.
        if (after >= bareEnd || mayFollowValue(text, after)) {
            out += text.sliced(from, width);
            out += kRedacted;
            out += text.sliced(close, width);
            return after;
        }
    }
    out += kRedacted;
    return bareEnd;
}

// key=value, in a query string or a header parameter, plain or percent-encoded.
QString redactAssignments(const QString &text)
{
    static const QRegularExpression key(
        QStringLiteral("(?:" STRMQT_CREDENTIAL_NAMES ")(=|%3d)"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator matches = key.globalMatch(text);
    if (!matches.hasNext())
        return text;
    QString out;
    out.reserve(text.size());
    qsizetype done = 0;
    // Carried from one credential to the next, so a line is read once however
    // many it holds. A value already redacted is not read at all: what a
    // secret contains says nothing about the line around it.
    Enclosures open;
    qsizetype read = 0;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (match.capturedStart() < done)
            continue; // inside a value that is already gone
        const qsizetype line = lineStart(text, match.capturedStart());
        if (line > read) {
            open.clear();
            read = line;
        }
        open.scan(QStringView(text).sliced(read, match.capturedStart() - read));
        out += QStringView(text).sliced(done, match.capturedEnd() - done);
        done = redactValue(text, match.capturedEnd(), match.capturedLength(1) > 1, open, out);
        read = done;
    }
    out += QStringView(text).sliced(done);
    return out;
}

// "key": "value", as a request or response body prints it — including through
// qDebug(), which escapes every quote. A JSON string may hold anything, so the
// value runs to its closing quote and nowhere else.
QString redactJsonFields(const QString &text)
{
    static const QRegularExpression field(
        QStringLiteral(R"re((\\"|["'])[^"'\\\s:=&,{}]{0,48}?(?:)re" STRMQT_CREDENTIAL_NAMES
                       R"re()\1\s*:\s*(?=\1))re"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator matches = field.globalMatch(text);
    if (!matches.hasNext())
        return text;
    QString out;
    out.reserve(text.size());
    qsizetype done = 0;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (match.capturedStart() < done)
            continue;
        const qsizetype from = match.capturedEnd();
        const Quote quote = quoteAt(text, from);
        const qsizetype width = quoteWidth(quote);
        const qsizetype close = closingQuote(text, from + width, quote);
        out += QStringView(text).sliced(done, from + width - done);
        out += kRedacted;
        done = close < 0 ? lineEnd(text, from) : close;
    }
    out += QStringView(text).sliced(done);
    return out;
}

#undef STRMQT_CREDENTIAL_NAMES

void strmqtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    const QString redacted = redactSensitiveText(msg);
    const QString formatted = qFormatLogMessage(type, context, redacted);
    fprintf(stderr, "%s\n", formatted.toLocal8Bit().constData());
    fflush(stderr);
}

} // namespace

namespace strmqt {

QString redactSensitiveText(const QString &text)
{
    // "Authorization: Bearer X" / "X-Emby-Token: X". A scheme word is kept so
    // the line still says what kind of credential it was.
    static const QRegularExpression headerCredential(
        QStringLiteral(
            R"(((?:authorization|x(?:-|%2d)emby(?:-|%2d)token)\s*:\s*(?:(?:bearer|basic)\s+)?)([^\s,;]+))"),
        QRegularExpression::CaseInsensitiveOption);

    // Bodies first: a JSON string is delimited by its quotes alone, and must
    // not be cut short by a "token=" the rule after this one finds inside it.
    QString redacted = redactAssignments(redactJsonFields(text));
    redacted.replace(headerCredential, QStringLiteral("\\1<redacted>"));
    return redacted;
}

void initLogging()
{
    qSetMessagePattern(QStringLiteral("%{time hh:mm:ss.zzz} %{category} %{type}: %{message}"));
    qInstallMessageHandler(strmqtMessageHandler);
}

} // namespace strmqt
