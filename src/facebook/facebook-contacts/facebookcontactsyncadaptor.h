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

#ifndef FACEBOOKCONTACTSYNCADAPTOR_H
#define FACEBOOKCONTACTSYNCADAPTOR_H

#include "facebookdatatypesyncadaptor.h"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QVariantMap>
#include <QtCore/QList>
#include <QtCore/QStringList>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QSslError>
#include <QtSql/QSqlDatabase>

#include <QtContacts/QContactManager>
#include <QtContacts/QContact>

#include <socialcache/facebookcontactsdatabase.h>

USE_CONTACTS_NAMESPACE

class FacebookContactImageDownloader;
class FacebookContactSyncAdaptor : public FacebookDataTypeSyncAdaptor
{
    Q_OBJECT

public:
    FacebookContactSyncAdaptor(QObject *parent);
    ~FacebookContactSyncAdaptor();

    QString syncServiceName() const;
    void sync(const QString &dataTypeString, int accountId);

protected: // implementing FacebookDataTypeSyncAdaptor interface
    void purgeDataForOldAccount(int oldId, SocialNetworkSyncAdaptor::PurgeMode mode);
    void beginSync(int accountId, const QString &accessToken);
    void finalize(int accountId);
    void finalCleanup();

private:
    void requestData(int accountId, const QString &accessToken,
                     const QString &continuationRequest = QString(),
                     const QDateTime &syncTimestamp = QDateTime());
    void purgeAccount(int accountId);

private Q_SLOTS:
    void friendsFinishedHandler();
    void slotImageDownloaded(const QString &url, const QString &path, const QVariantMap &data);

private:
    QContactManager *m_contactManager;
    FacebookContactImageDownloader *m_workerObject;
    QMap<int, QList<QContact> > m_remoteContacts; // accountId to contacts to save.
    QMap<int, QList<QPair<QString, QVariantMap> > > m_queuedAvatarDownloads;

    QList<QContactId> contactIdsForGuid(const QString &fbuid);
    QContact newOrExistingContact(const QString &fbuid, bool *isNewContact);
    QContact parseContactDetails(const QJsonObject &blobDetails, int accountId, bool *needsSaving);
    bool storeToLocal(const QString &accessToken, int accountId, int *addedCount, int *modifiedCount, int *removedCount, int *unchangedCount);
    bool remoteContactDiffersFromLocal(const QContact &remoteContact, const QContact &localContact) const;
};

#endif // FACEBOOKCONTACTSYNCADAPTOR_H
