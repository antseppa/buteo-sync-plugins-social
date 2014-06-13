/****************************************************************************
 **
 ** Copyright (C) 2013 Jolla Ltd.
 ** Contact: Chris Adams <chris.adams@jollamobile.com>
 **
 ****************************************************************************/

#include "vknotificationsyncadaptor.h"
#include "trace.h"

#include <QUrlQuery>
#include <QDebug>

//static const int OLD_NOTIFICATION_LIMIT_IN_DAYS = 21; // TODO

VKNotificationSyncAdaptor::VKNotificationSyncAdaptor(QObject *parent)
    : VKDataTypeSyncAdaptor(SocialNetworkSyncAdaptor::Notifications, parent)
{
    setInitialActive(true);
}

VKNotificationSyncAdaptor::~VKNotificationSyncAdaptor()
{
}

QString VKNotificationSyncAdaptor::syncServiceName() const
{
    return QStringLiteral("vk-microblog");
}

void VKNotificationSyncAdaptor::purgeDataForOldAccounts(const QList<int> &purgeIds)
{
    Q_UNUSED(purgeIds);
    if (purgeIds.size()) {
        foreach (int accountIdentifier, purgeIds) {
            m_db.removeNotifications(accountIdentifier);
        }
        m_db.sync();
        m_db.wait();
    }
}

void VKNotificationSyncAdaptor::beginSync(int accountId, const QString &accessToken)
{
    requestNotifications(accountId, accessToken);
}

void VKNotificationSyncAdaptor::finalize(int accountId)
{
    Q_UNUSED(accountId);
    //m_db.purgeOldNotifications(OLD_NOTIFICATION_LIMIT_IN_DAYS); // TODO
    m_db.sync();
    m_db.wait();
}

void VKNotificationSyncAdaptor::requestNotifications(int accountId, const QString &accessToken, const QString &until, const QString &pagingToken)
{
    // TODO: result paging
    Q_UNUSED(until);
    Q_UNUSED(pagingToken);

    QList<QPair<QString, QString> > queryItems;
    queryItems.append(QPair<QString, QString>(QString(QLatin1String("access_token")), accessToken));
    queryItems.append(QPair<QString, QString>(QString(QLatin1String("v")), QStringLiteral("5.21"))); // version

    QUrl url(QStringLiteral("https://api.vk.com/method/notifications.get"));
    QUrlQuery query(url);
    query.setQueryItems(queryItems);
    url.setQuery(query);
    QNetworkReply *reply = networkAccessManager->get(QNetworkRequest(url));

    if (reply) {
        reply->setProperty("accountId", accountId);
        reply->setProperty("accessToken", accessToken);
        connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(errorHandler(QNetworkReply::NetworkError)));
        connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(sslErrorsHandler(QList<QSslError>)));
        connect(reply, SIGNAL(finished()), this, SLOT(finishedHandler()));

        // we're requesting data.  Increment the semaphore so that we know we're still busy.
        incrementSemaphore(accountId);
        setupReplyTimeout(accountId, reply);
    } else {
        TRACE(SOCIALD_ERROR,
                QString(QStringLiteral("error: unable to request home posts from VK account with id %1"))
                .arg(accountId));
    }
}

void VKNotificationSyncAdaptor::finishedHandler()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    bool isError = reply->property("isError").toBool();
    int accountId = reply->property("accountId").toInt();
    QByteArray replyData = reply->readAll();
    disconnect(reply);
    reply->deleteLater();
    removeReplyTimeout(accountId, reply);

    bool ok = false;
    QJsonObject parsed = parseJsonObjectReplyData(replyData, &ok);

    if (!isError && ok && parsed.contains(QLatin1String("response"))) {
        QJsonObject responseObj = parsed.value(QStringLiteral("response")).toObject();

        QJsonArray profileValues = responseObj.value(QStringLiteral("profiles")).toArray();
        QList<UserProfile> userProfiles;
        foreach (const QJsonValue &entry, profileValues) {
            userProfiles << UserProfile::fromJsonObject(entry.toObject());
        }

        QJsonArray items = responseObj.value(QLatin1String("items")).toArray();
        foreach (const QJsonValue &entry, items) {
            QJsonObject object = entry.toObject();
            if (!object.isEmpty()) {
                saveVKNotificationFromObject(accountId, object, userProfiles);
            }
        }
    } else {
        // error occurred during request.
        TRACE(SOCIALD_ERROR,
                QString(QLatin1String("error: unable to parse notification data from request with account %1; got: %2"))
                .arg(accountId).arg(QString::fromLatin1(replyData.constData())));
    }

    // we're finished this request.  Decrement our busy semaphore.
    decrementSemaphore(accountId);
}

void VKNotificationSyncAdaptor::saveVKNotificationFromObject(int accountId, const QJsonObject &notif, const QList<UserProfile> &userProfiles)
{
    QString type = notif.value("type").toString();
    QDateTime timestamp = parseVKDateTime(notif.value(QStringLiteral("date")));
    QJsonObject feedback = notif.value(QStringLiteral("feedback")).toObject();
    Q_FOREACH (const QJsonValue &feedbackItem, feedback.value(QStringLiteral("items")).toArray()) {
        QJsonObject feedbackItemObj = feedbackItem.toObject();
        int fromId = int(feedbackItemObj.value(QStringLiteral("from_id")).toDouble());
        int toId = int(feedbackItemObj.value(QStringLiteral("to_id")).toDouble());
        UserProfile profile = findProfile(userProfiles, fromId);
        if (profile.uid != 0) {
            m_db.addVKNotification(accountId, type, QString::number(fromId), profile.name(), profile.icon, QString::number(toId), timestamp);
        } else {
            TRACE(SOCIALD_DEBUG,
                    QString(QLatin1String("no user profile found for owner %1 of notification from account %2"))
                    .arg(fromId).arg(accountId));
        }
    }
}
