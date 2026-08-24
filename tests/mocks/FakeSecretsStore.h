#pragma once

#include "platform/SecretsStore.h"

namespace strmqt::test {

class FakeSecretsStore final : public SecretsStore
{
public:
    enum class CallType
    {
        NetworkWallet,
        Open,
        Write,
        Read,
        Remove,
    };

    struct Call
    {
        CallType type;
        QString key;
        QString value;
    };

    explicit FakeSecretsStore(QObject *parent = nullptr) : SecretsStore(parent) {}

    bool available = true;
    QList<Call> calls;

    void replyNetworkWallet(bool success, const QString &name = QStringLiteral("kdewallet"),
                            const QString &error = QStringLiteral("networkWallet failed"))
    {
        completeNetworkWallet(success, name, error);
    }

    void replyOpen(bool success, int handle = 17,
                   const QString &error = QStringLiteral("open failed"))
    {
        completeOpenWallet(success, handle, error);
    }

    void replyWrite(bool success, const QString &error = QStringLiteral("write failed"))
    {
        completeWritePassword(success, error);
    }

    void replyRead(bool success, const QString &value = {},
                   const QString &error = QStringLiteral("read failed"))
    {
        completeReadPassword(success, value, error);
    }

    void replyRemove(bool success, const QString &error = QStringLiteral("remove failed"))
    {
        completeRemoveEntry(success, error);
    }

protected:
    bool walletTransportAvailable() const override { return available; }

    void requestNetworkWallet() override { calls.append({CallType::NetworkWallet, {}, {}}); }

    void requestOpenWallet(const QString &walletName) override
    {
        calls.append({CallType::Open, walletName, {}});
    }

    void requestWritePassword(const QString &key, const QString &value) override
    {
        calls.append({CallType::Write, key, value});
    }

    void requestReadPassword(const QString &key) override
    {
        calls.append({CallType::Read, key, {}});
    }

    void requestRemoveEntry(const QString &key) override
    {
        calls.append({CallType::Remove, key, {}});
    }
};

} // namespace strmqt::test
