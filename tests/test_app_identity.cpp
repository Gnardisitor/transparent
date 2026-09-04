#include <QtTest>

#include "core/AppIdentity.h"

#include <QCoreApplication>
#include <QSettings>

// AppIdentity::init() must run before any QSettings is constructed. A
// default-constructed QSettings is then writable, error free, and keeps
// values across instances, which is what stands in for an app restart here.
class TestAppIdentity : public QObject {
    Q_OBJECT

private slots:
    void settingsAreWritableAfterInit();
    void valuesSurviveASettingsRoundTrip();
};

void TestAppIdentity::settingsAreWritableAfterInit() {
    AppIdentity::init();
    QCOMPARE(QCoreApplication::organizationName(), QStringLiteral("transparent"));
    QCOMPARE(QCoreApplication::organizationDomain(), QStringLiteral("transparent.com"));
    QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("transparent"));

    QSettings settings;
    QCOMPARE(settings.status(), QSettings::NoError);
    QVERIFY(settings.isWritable());
    QVERIFY(!settings.fileName().isEmpty());
}

void TestAppIdentity::valuesSurviveASettingsRoundTrip() {
    AppIdentity::init();
    {
        QSettings settings;
        settings.setValue(QStringLiteral("appidentity/probe"), QStringLiteral("round-trip"));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    {
        // A fresh QSettings instance stands in for the next app launch.
        QSettings settings;
        QCOMPARE(settings.value(QStringLiteral("appidentity/probe")).toString(),
                 QStringLiteral("round-trip"));
        settings.remove(QStringLiteral("appidentity/probe"));
    }
}

QTEST_GUILESS_MAIN(TestAppIdentity)
#include "test_app_identity.moc"
