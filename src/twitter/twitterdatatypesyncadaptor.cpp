/****************************************************************************
 **
 ** Copyright (C) 2013 Jolla Ltd.
 ** Contact: Chris Adams <chris.adams@jollamobile.com>
 **
 ****************************************************************************/

#include "twitterdatatypesyncadaptor.h"
#include "trace.h"

#include <QtCore/QDebug>

#include <QtCore/QVariantMap>
#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QUuid>
#include <QtCore/qmath.h>

#include <QCryptographicHash>
#include <QJsonDocument>

//libsailfishkeyprovider
#include <sailfishkeyprovider.h>

// sailfish-components-accounts-qt5
#include <accountmanager.h>
#include <account.h>
#include <signinparameters.h>

//libsignon-qt: SignOn::NoUserInteractionPolicy
#include <SignOn/SessionData>

TwitterDataTypeSyncAdaptor::TwitterDataTypeSyncAdaptor(SocialNetworkSyncAdaptor::DataType dataType, QObject *parent)
    : SocialNetworkSyncAdaptor("twitter", dataType, parent), m_triedLoading(false)
{
}

TwitterDataTypeSyncAdaptor::~TwitterDataTypeSyncAdaptor()
{
}

void TwitterDataTypeSyncAdaptor::sync(const QString &dataTypeString, int accountId)
{
    if (dataTypeString != SocialNetworkSyncAdaptor::dataTypeName(dataType)) {
        TRACE(SOCIALD_ERROR,
                QString(QLatin1String("error: Twitter %1 sync adaptor was asked to sync %2"))
                .arg(SocialNetworkSyncAdaptor::dataTypeName(dataType)).arg(dataTypeString));
        setStatus(SocialNetworkSyncAdaptor::Error);
        return;
    }

    if (consumerKey().isEmpty() || consumerSecret().isEmpty()) {
        TRACE(SOCIALD_ERROR, QString(QLatin1String("error: secrets could not be retrieved for twitter")));
        setStatus(SocialNetworkSyncAdaptor::Error);
        return;
    }

    // either a single-account sync, or an all-account sync.
    // an all-account sync is a three stage process.
    // 1) if an account has been removed, we need to purge the data we retrieved with it
    // 2) if an account has been added, we need to pull data for the account
    // 3) for existing accounts, pull new data for the existing account
    setStatus(SocialNetworkSyncAdaptor::Busy);

    QList<int> newIds, purgeIds, updateIds;
    if (accountId == 0) {
        // all account sync.  determine accounts added/removed/need updating.
        checkAccounts(dataType, &newIds, &purgeIds, &updateIds);

        // We only actually perform the purge operation for all-account (template) syncs.
        purgeDataForOldAccounts(purgeIds); // call the derived-class purge entrypoint.

        TRACE(SOCIALD_DEBUG,
                QString(QLatin1String("successfully triggered sync of %1: purged %2 accounts"))
                .arg(SocialNetworkSyncAdaptor::dataTypeName(dataType)).arg(purgeIds.size()));

        setFinishedInactive(); // just had to purge, and we're done.
    } else {
        // single account sync.
        updateDataForAccounts(QList<int>() << accountId);

        TRACE(SOCIALD_DEBUG,
                QString(QLatin1String("successfully triggered sync with profile: %1"))
                .arg(m_accountSyncProfile->name()));
    }
}

void TwitterDataTypeSyncAdaptor::updateDataForAccounts(const QList<int> &accountIds)
{
    if (accountIds.size() != 1) {
        // Since the "split monolithic plugin" refactoring, this function
        // should only ever be called for a single account.
        // TODO: refactor all of the plugins even more completely, to
        // remove the per-accountId state data (maps etc) and "fix" the
        // function signatures to match the new per-account paradigm.
        qWarning() << Q_FUNC_INFO << "called with multiple accounts - ERROR!";
        setStatus(SocialNetworkSyncAdaptor::Error);
        return;
    }

    foreach (int accountId, accountIds) {
        // will be decremented by either signOnError or signOnResponse.
        // we increment them prior to the loop below to avoid spurious
        // "all are zero" causing setFinishedInactive() too early,
        // if one of the accounts could not be loaded.
        incrementSemaphore(accountId);
    }

    foreach (int accountId, accountIds) {
        Account *account = accountManager->account(accountId);
        if (!account) {
            TRACE(SOCIALD_ERROR,
                    QString(QLatin1String("error: existing account with id %1 couldn't be retrieved"))
                    .arg(accountId));
            setStatus(SocialNetworkSyncAdaptor::Error);
            decrementSemaphore(accountId);
            continue;
        }
        if (account->status() == Account::Initialized || account->status() == Account::Synced) {
            signIn(account);
        } else {
            connect(account, SIGNAL(statusChanged()), this, SLOT(accountStatusChangeHandler()));
        }
    }
}

void TwitterDataTypeSyncAdaptor::accountCredentialsChangeHandler()
{
    Account *account = qobject_cast<Account*>(sender());
    if (account->status() == Account::Initialized) {
        setCredentialsNeedUpdate(account);
    }
}

void TwitterDataTypeSyncAdaptor::accountStatusChangeHandler()
{
    Account *account = qobject_cast<Account*>(sender());
    if (account->status() == Account::Initialized || account->status() == Account::Synced)
    {
        // Not anymore interested about status changes of this account instance
        account->disconnect(this);
        signIn(account);
    }
}

void TwitterDataTypeSyncAdaptor::signOnError(const QString &err, int errorType)
{
    Account *account = qobject_cast<Account*>(sender());
    int accountId = account->identifier();
    TRACE(SOCIALD_ERROR,
            QString(QLatin1String("error: credentials for account with id %1 couldn't be retrieved:"))
          .arg(accountId) << err);
    setStatus(SocialNetworkSyncAdaptor::Error);

    // if the error is because credentials have expired, we
    // set the CredentialsNeedUpdate key.
    if (errorType == Account::SignInCredentialsExpiredError) {
        setCredentialsNeedUpdate(account);
    } else {
        account->disconnect(this);
   }

    // if we couldn't sign in, we can't sync with this account.
    decrementSemaphore(accountId);
}

void TwitterDataTypeSyncAdaptor::signOnResponse(const QVariantMap &data)
{
    QString oauthToken;
    QString oauthTokenSecret;
    Account *account = qobject_cast<Account*>(sender());
    int accountId = account->identifier();

    if (data.contains(QLatin1String("AccessToken"))) {
        oauthToken = data.value(QLatin1String("AccessToken")).toString();
    } else {
        TRACE(SOCIALD_INFORMATION,
                QString(QLatin1String("signon response for account with id %1 contained no oauth token"))
                .arg(accountId));
    }

    if (data.contains(QLatin1String("TokenSecret"))) {
        oauthTokenSecret = data.value(QLatin1String("TokenSecret")).toString();
    } else {
        TRACE(SOCIALD_INFORMATION,
                QString(QLatin1String("signon response for account with id %1 contained no oauth token secret"))
                .arg(accountId));
    }

    account->disconnect(this);
    if (!oauthToken.isEmpty() && !oauthTokenSecret.isEmpty()) {
        beginSync(accountId, oauthToken, oauthTokenSecret); // call the derived-class sync entrypoint.
    }

    decrementSemaphore(accountId);
}

QString TwitterDataTypeSyncAdaptor::consumerKey()
{
    if (!m_triedLoading) {
        loadConsumerKeyAndSecret();
    }
    return m_consumerKey;
}

QString TwitterDataTypeSyncAdaptor::consumerSecret()
{
    if (!m_triedLoading) {
        loadConsumerKeyAndSecret();
    }
    return m_consumerSecret;
}

void TwitterDataTypeSyncAdaptor::errorHandler(QNetworkReply::NetworkError err)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    QByteArray replyData = reply->readAll();
    int accountId = reply->property("accountId").toInt();

    TRACE(SOCIALD_ERROR,
            QString(QLatin1String("error: %1 request with account %2 experienced error: %3"))
            .arg(SocialNetworkSyncAdaptor::dataTypeName(dataType)).arg(accountId).arg(err));
    // set "isError" on the reply so that adapters know to ignore the result in the finished() handler
    reply->setProperty("isError", QVariant::fromValue<bool>(true));
    // Note: not all errors are "unrecoverable" errors, so we don't change the status here.

    bool ok = false;
    QJsonObject parsed = parseJsonObjectReplyData(replyData, &ok);
    if (ok && parsed.contains(QLatin1String("errors"))) {
        QJsonArray dataList = parsed.value(QLatin1String("errors")).toArray();
        // API v1.1 returns only one element in the array, but looks like these
        // are constantly updated: https://dev.twitter.com/docs/error-codes-responses
        foreach (QJsonValue data, dataList) {
            QJsonObject dataMap = data.toObject();
            if (dataMap.value("code").toDouble() == 32) {
                Account *account = accountManager->account(accountId);
                if (account->status() == Account::Initialized) {
                    setCredentialsNeedUpdate(account);
                } else {
                    connect(account, SIGNAL(statusChanged()), this, SLOT(accountCredentialsChangeHandler()));
                }
                return;
            }
        }
    }
}

void TwitterDataTypeSyncAdaptor::sslErrorsHandler(const QList<QSslError> &errs)
{
    QString sslerrs;
    foreach (const QSslError &e, errs) {
        sslerrs += e.errorString() + "; ";
    }
    if (errs.size() > 0) {
        sslerrs.chop(2);
    }
    TRACE(SOCIALD_ERROR,
            QString(QLatin1String("error: %1 request with account %2 experienced ssl errors: %3"))
            .arg(SocialNetworkSyncAdaptor::dataTypeName(dataType)).arg(sender()->property("accountId").toInt()).arg(sslerrs));
    // set "isError" on the reply so that adapters know to ignore the result in the finished() handler
    sender()->setProperty("isError", QVariant::fromValue<bool>(true));
    // Note: not all errors are "unrecoverable" errors, so we don't change the status here.
}

// This function taken from http://qt-project.org/wiki/HMAC-SHA1 which is in the public domain
// and carries no licensing requirements (as at 2013-05-09)
static QString hmacSha1(const QString &signingKey, const QString &baseString)
{
    QByteArray key = signingKey.toLatin1();
    QByteArray baseArray = baseString.toLatin1();

    int blockSize = 64; // HMAC-SHA-1 block size, defined in SHA-1 standard
    if (key.length() > blockSize) { // if key is longer than block size (64), reduce key length with SHA-1 compression
        key = QCryptographicHash::hash(key, QCryptographicHash::Sha1);
    }

    QByteArray innerPadding(blockSize, char(0x36)); // initialize inner padding with char "6"
    QByteArray outerPadding(blockSize, char(0x5c)); // initialize outer padding with char "\"
    // ascii characters 0x36 ("6") and 0x5c ("\") are selected because they have large
    // Hamming distance (http://en.wikipedia.org/wiki/Hamming_distance)

    for (int i = 0; i < key.length(); i++) {
        innerPadding[i] = innerPadding[i] ^ key.at(i); // XOR operation between every byte in key and innerpadding, of key length
        outerPadding[i] = outerPadding[i] ^ key.at(i); // XOR operation between every byte in key and outerpadding, of key length
    }

    // result = hash ( outerPadding CONCAT hash ( innerPadding CONCAT baseArray ) ).toBase64
    QByteArray total = outerPadding;
    QByteArray part = innerPadding;
    part.append(baseArray);
    total.append(QCryptographicHash::hash(part, QCryptographicHash::Sha1));
    QByteArray hashed = QCryptographicHash::hash(total, QCryptographicHash::Sha1);
    return hashed.toBase64();
}

QString TwitterDataTypeSyncAdaptor::authorizationHeader(int accountId, const QString &oauthToken, const QString &oauthTokenSecret, const QString &requestMethod, const QString &requestUrl, const QList<QPair<QString, QString> > &parameters)
{
    Q_UNUSED(accountId);

    // Twitter requires all requests to be signed with an authorization header.
    QString key = consumerKey();
    QString secret = consumerSecret();

    if (key.isEmpty() || secret.isEmpty()) {
        return QString();
    }

    QString oauthNonce = QString::fromLatin1(QUuid::createUuid().toByteArray().toBase64());
    QString oauthSignature;
    QString oauthSigMethod = QLatin1String("HMAC-SHA1");
    QString oauthTimestamp = QString::number(qFloor(QDateTime::currentMSecsSinceEpoch() / 1000.0));
    //QString oauthToken; // already passed in as parameter.
    QString oauthVersion = QLatin1String("1.0");

    // now build up the encoded parameters map.  We use a map to perform alphabetical sorting.
    QMap<QString, QString> encodedParams;
    encodedParams.insert(QUrl::toPercentEncoding(QLatin1String("oauth_consumer_key")),
                         QUrl::toPercentEncoding(key));
    encodedParams.insert(QUrl::toPercentEncoding(QLatin1String("oauth_nonce")),
                         QUrl::toPercentEncoding(oauthNonce));
    encodedParams.insert(QUrl::toPercentEncoding(QLatin1String("oauth_signature_method")),
                         QUrl::toPercentEncoding(oauthSigMethod));
    encodedParams.insert(QUrl::toPercentEncoding(QLatin1String("oauth_timestamp")),
                         QUrl::toPercentEncoding(oauthTimestamp));
    encodedParams.insert(QUrl::toPercentEncoding(QLatin1String("oauth_token")),
                         QUrl::toPercentEncoding(oauthToken));
    encodedParams.insert(QUrl::toPercentEncoding(QLatin1String("oauth_version")),
                         QUrl::toPercentEncoding(oauthVersion));
    for (int i = 0; i < parameters.size(); ++i) {
        QPair<QString, QString> param = parameters.at(i);
        encodedParams.insert(QUrl::toPercentEncoding(param.first),
                             QUrl::toPercentEncoding(param.second));
    }

    QString parametersString;
    QStringList keys = encodedParams.keys();
    foreach (const QString &key, keys) {
        parametersString += key + QLatin1String("=") + encodedParams.value(key) + QLatin1String("&");
    }
    parametersString.chop(1);

    QString signatureBaseString = requestMethod.toUpper() + QLatin1String("&")
                                + QUrl::toPercentEncoding(requestUrl) + QLatin1String("&")
                                + QUrl::toPercentEncoding(parametersString);

    QString signingKey = QUrl::toPercentEncoding(secret) + QLatin1String("&")
                       + QUrl::toPercentEncoding(oauthTokenSecret);

    oauthSignature = hmacSha1(signingKey, signatureBaseString);
    encodedParams.insert(QUrl::toPercentEncoding(QLatin1String("oauth_signature")),
                         QUrl::toPercentEncoding(oauthSignature));

    // now generate the Authorization header from the encoded parameters map.
    // we need to remove the query items from the encoded parameters map first.
    QString authHeader = QLatin1String("OAuth ");
    for (int i = 0; i < parameters.size(); ++i) {
        QPair<QString, QString> param = parameters.at(i);
        encodedParams.remove(QUrl::toPercentEncoding(param.first));
    }
    keys = encodedParams.keys();
    foreach (const QString &key, keys) {
        authHeader += key + QLatin1String("=\"") + encodedParams.value(key) + QLatin1String("\", ");
    }
    authHeader.chop(2);
    return authHeader;
}

QDateTime TwitterDataTypeSyncAdaptor::parseTwitterDateTime(const QString &tdt)
{
    // Twitter use the following format ddd MMM dd hh:mm:ss +0000 yyyy
    // The +0000 should always be +0000 since it relates to UTC time
    // We are using it like that but it might break if Twitter change their
    // API or if +0000 is not constant.
    // Twitter use english in their date, so we need to use an english
    // locale to parse the date

    QLocale locale (QLocale::English, QLocale::UnitedStates);
    QDateTime time = locale.toDateTime(tdt, "ddd MMM dd HH:mm:ss +0000 yyyy");
    time.setTimeSpec(Qt::UTC);

    return time;
}

void TwitterDataTypeSyncAdaptor::loadConsumerKeyAndSecret()
{
    m_triedLoading = true;
    char *cConsumerKey = NULL;
    char *cConsumerSecret = NULL;
    int ckSuccess = SailfishKeyProvider_storedKey("twitter", "twitter-sync", "consumer_key", &cConsumerKey);
    int csSuccess = SailfishKeyProvider_storedKey("twitter", "twitter-sync", "consumer_secret", &cConsumerSecret);

    if (ckSuccess != 0 || cConsumerKey == NULL || csSuccess != 0 || cConsumerSecret == NULL) {
        TRACE(SOCIALD_INFORMATION, QLatin1String("No valid OAuth2 keys found"));
        return;
    }

    m_consumerKey = QLatin1String(cConsumerKey);
    m_consumerSecret = QLatin1String(cConsumerSecret);
    free(cConsumerKey);
    free(cConsumerSecret);
}

void TwitterDataTypeSyncAdaptor::setCredentialsNeedUpdate(Account *account)
{
    // Not anymore interested about status changes of this account instance
    account->disconnect(this);
    qWarning() << "sociald:Twitter: setting CredentialsNeedUpdate to true for account:" << account->identifier();
    account->setConfigurationValue(syncServiceName(), "CredentialsNeedUpdate", QVariant::fromValue<bool>(true));
    account->setConfigurationValue(syncServiceName(), "CredentialsNeedUpdateFrom", QVariant::fromValue<QString>(QString::fromLatin1("sociald-twitter")));
    account->sync();
}

void TwitterDataTypeSyncAdaptor::signIn(Account *account)
{
    // grab out a valid identity for the sync service.
    if (!account->isEnabledWithService(syncServiceName())) {
        TRACE(SOCIALD_INFORMATION,
              QString(QLatin1String("account with id %1 is not enabled with service %2"))
              .arg(account->identifier()).arg(syncServiceName()));
        decrementSemaphore(account->identifier());
        return;
    }

    // Fetch consumer key and secret from keyprovider
    QString key = consumerKey();
    QString secret = consumerSecret();
    if (key.isEmpty() || secret.isEmpty()) {
        decrementSemaphore(account->identifier());
        return;
    }

    SignInParameters *sip = account->signInParameters(syncServiceName());
    sip->setParameter(QLatin1String("ConsumerKey"), key);
    sip->setParameter(QLatin1String("ConsumerSecret"), secret);
    sip->setParameter(QLatin1String("UiPolicy"), SignInParameters::NoUserInteractionPolicy);

    connect(account, SIGNAL(signInError(QString,int)), this, SLOT(signOnError(QString,int)));
    connect(account, SIGNAL(signInResponse(QVariantMap)), this, SLOT(signOnResponse(QVariantMap)));
    account->signIn("Jolla", "Jolla", sip);
}
