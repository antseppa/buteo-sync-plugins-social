/****************************************************************************
 **
 ** Copyright (C) 2013-2014 Jolla Ltd.
 ** Contact: Chris Adams <chris.adams@jollamobile.com>
 **
 ** This program/library is free software; you can redistribute it and/or
 ** modify it under the terms of the GNU Lesser General Public License
 ** version 2.1 as published by the Free Software Foundation.
 **
 ** This program/library is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 ** Lesser General Public License for more details.
 **
 ** You should have received a copy of the GNU Lesser General Public
 ** License along with this program/library; if not, write to the Free
 ** Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 ** 02110-1301 USA
 **
 ****************************************************************************/

#include "twittermentiontimelinesyncadaptor.h"
#include "trace.h"

#include <QtCore/QPair>
#include <QtCore/QUrlQuery>
#include <QtCore/QJsonValue>

//nemo-qml-plugins/notifications
#include <notification.h>

#define SOCIALD_TWITTER_MENTIONS_ID_PREFIX QLatin1String("twitter-mentions-")
#define SOCIALD_TWITTER_MENTIONS_GROUPNAME QLatin1String("sociald-sync-twitter-mentions")

// currently, we integrate with the device notifications via nemo-qml-plugin-notification

TwitterMentionTimelineSyncAdaptor::TwitterMentionTimelineSyncAdaptor(QObject *parent)
    : TwitterDataTypeSyncAdaptor(SocialNetworkSyncAdaptor::Notifications, parent)
{
    // can sync, enabled
    setInitialActive(true);
}

TwitterMentionTimelineSyncAdaptor::~TwitterMentionTimelineSyncAdaptor()
{
}

QString TwitterMentionTimelineSyncAdaptor::syncServiceName() const
{
    return QStringLiteral("twitter-microblog");
}

void TwitterMentionTimelineSyncAdaptor::purgeDataForOldAccount(int oldId, SocialNetworkSyncAdaptor::PurgeMode)
{
    Notification *notification = findNotification(oldId);
    if (notification) {
        notification->close();
        notification->deleteLater();
    }
}

void TwitterMentionTimelineSyncAdaptor::beginSync(int accountId, const QString &oauthToken, const QString &oauthTokenSecret)
{
    requestNotifications(accountId, oauthToken, oauthTokenSecret);
}

void TwitterMentionTimelineSyncAdaptor::requestNotifications(int accountId, const QString &oauthToken, const QString &oauthTokenSecret, const QString &sinceTweetId)
{
    QList<QPair<QString, QString> > queryItems;
    queryItems.append(QPair<QString, QString>(QString(QLatin1String("count")), QString(QLatin1String("50"))));
    if (!sinceTweetId.isEmpty()) {
        queryItems.append(QPair<QString, QString>(QString(QLatin1String("since_id")), sinceTweetId));
    }
    QString baseUrl = QLatin1String("https://api.twitter.com/1.1/statuses/mentions_timeline.json");
    QUrl url(baseUrl);
    QUrlQuery query(url);
    query.setQueryItems(queryItems);
    url.setQuery(query);

    QNetworkRequest nreq(url);
    nreq.setRawHeader("Authorization", authorizationHeader(
            accountId, oauthToken, oauthTokenSecret,
            QLatin1String("GET"), baseUrl, queryItems).toLatin1());

    QNetworkReply *reply = m_networkAccessManager->get(nreq);
    
    if (reply) {
        reply->setProperty("accountId", accountId);
        reply->setProperty("oauthToken", oauthToken);
        reply->setProperty("oauthTokenSecret", oauthTokenSecret);
        connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(errorHandler(QNetworkReply::NetworkError)));
        connect(reply, SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(sslErrorsHandler(QList<QSslError>)));
        connect(reply, SIGNAL(finished()), this, SLOT(finishedHandler()));

        // we're requesting data.  Increment the semaphore so that we know we're still busy.
        incrementSemaphore(accountId);
        setupReplyTimeout(accountId, reply);
    } else {
        SOCIALD_LOG_ERROR("unable to request mention timeline notifications from Twitter account with id" << accountId);
    }
}

void TwitterMentionTimelineSyncAdaptor::finishedHandler()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    int accountId = reply->property("accountId").toInt();
    QDateTime lastSync = lastSyncTimestamp(QLatin1String("twitter"),
                                           SocialNetworkSyncAdaptor::dataTypeName(SocialNetworkSyncAdaptor::Notifications),
                                           accountId);
    SOCIALD_LOG_TRACE("last sync:" << lastSync);

    QByteArray replyData = reply->readAll();
    disconnect(reply);
    reply->deleteLater();
    removeReplyTimeout(accountId, reply);

    bool ok = false;
    QJsonArray tweets = parseJsonArrayReplyData(replyData, &ok);
    if (ok) {
        if (!tweets.size()) {
            SOCIALD_LOG_DEBUG("no notifications received for account" << accountId);
            decrementSemaphore(accountId);
            return;
        }

        int mentionsCount = 0;
        QString body;
        QString summary;
        QDateTime timestamp;
        QString link;
        foreach (const QJsonValue &tweetValue, tweets) {
            QJsonObject tweet = tweetValue.toObject();
            QDateTime createdTime = parseTwitterDateTime(tweet.value(QLatin1String("created_at")).toString());
            QString mentionId = tweet.value(QLatin1String("id_str")).toString();
            QString text = tweet.value(QLatin1String("text")).toString();
            QJsonObject user = tweet.value(QLatin1String("user")).toObject();
            QString userName = user.value(QLatin1String("name")).toString();
            QString userScreenName = user.value(QLatin1String("screen_name")).toString();

            // check to see if we need to post it to the notifications feed
            int sinceSpan = m_accountSyncProfile
                          ? m_accountSyncProfile->key(Buteo::KEY_SYNC_SINCE_DAYS_PAST, QStringLiteral("7")).toInt()
                          : 7;
            if (lastSync.isValid() && createdTime < lastSync) {
                SOCIALD_LOG_DEBUG("notification for account" << accountId << "came after last sync:" << createdTime << ":" << text);
                break; // all subsequent notifications will be even older.
            } else if (createdTime.daysTo(QDateTime::currentDateTimeUtc()) > sinceSpan) {
                SOCIALD_LOG_DEBUG("mention for account" << accountId << "is more than" << sinceSpan << "days old:" << createdTime << ":" << text);
            } else {
                body = userName;
                summary = text;
                timestamp = createdTime;
                link = QLatin1String("https://mobile.twitter.com/") + userScreenName + QLatin1String("/status/") + mentionId;
                mentionsCount ++;
            }
        }

        if (mentionsCount > 0) {
            // Search if we already have a notification
            Notification *notification = createNotification(accountId);

            // Set properties of the notification
            notification->setItemCount(notification->itemCount() + mentionsCount);
            notification->setRemoteDBusCallServiceName("org.sailfishos.browser");
            notification->setRemoteDBusCallObjectPath("/");
            notification->setRemoteDBusCallInterface("org.sailfishos.browser");
            notification->setRemoteDBusCallMethodName("openUrl");
            QStringList openUrlArgs;


            if (notification->itemCount() == 1) {
                notification->setTimestamp(timestamp);
                notification->setSummary(summary);
                notification->setBody(body);
                openUrlArgs << link;
            } else {
                notification->setTimestamp(QDateTime::currentDateTimeUtc());
                // TODO: maybe we should display the name of the account
                //% "Twitter"
                notification->setBody(qtTrId("qtn_social_notifications_twitter"));
                //% "You received %n mentions"
                notification->setSummary(qtTrId("qtn_social_notifications_n_mentions", notification->itemCount()));
                openUrlArgs << QLatin1String("https://mobile.twitter.com/i/connect");
            }
            notification->setRemoteDBusCallArguments(QVariantList() << openUrlArgs);
            notification->publish();

            qlonglong localId = (0 + notification->replacesId());
            if (localId == 0) {
                // failed.
                SOCIALD_LOG_ERROR("failed to publish notification:" <<  body);
            }
        }
    } else {
        // error occurred during request.
        SOCIALD_LOG_ERROR("unable to parse notification data from request with account" << accountId << "," <<
                          "got:" << QString::fromLatin1(replyData.constData()));
    }

    // we're finished this request.  Decrement our busy semaphore.
    decrementSemaphore(accountId);
}

Notification *TwitterMentionTimelineSyncAdaptor::createNotification(int accountId)
{
    Notification *notification = findNotification(accountId);
    if (notification) {
        return notification;
    }

    notification = new Notification(this);
    notification->setCategory(QLatin1String("x-nemo.social.twitter.mention"));
    notification->setHintValue("x-nemo.sociald.account-id", accountId);
    return notification;
}

Notification * TwitterMentionTimelineSyncAdaptor::findNotification(int accountId)
{
    Notification *notification = 0;
    QList<QObject *> notifications = Notification::notifications();
    foreach (QObject *object, notifications) {
        Notification *castedNotification = static_cast<Notification *>(object);
        if (castedNotification->category() == "x-nemo.social.twitter.mention"
            && castedNotification->hintValue("x-nemo.sociald.account-id").toInt() == accountId) {
            notification = castedNotification;
            break;
        }
    }

    if (notification) {
        notifications.removeAll(notification);
    }

    qDeleteAll(notifications);

    return notification;
}
