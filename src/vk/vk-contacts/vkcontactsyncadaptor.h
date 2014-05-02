/****************************************************************************
 **
 ** Copyright (C) 2014 Jolla Ltd.
 ** Contact: Chris Adams <chris.adams@jollamobile.com>
 **
 ****************************************************************************/

#ifndef VKCONTACTSYNCADAPTOR_H
#define VKCONTACTSYNCADAPTOR_H

#include "vkdatatypesyncadaptor.h"

#include <twowaycontactsyncadapter.h>

#include <QContactManager>
#include <QContact>
#include <QDateTime>
#include <QList>
#include <QPair>

QTCONTACTS_USE_NAMESPACE

class VKContactImageDownloader;
class VKContactSyncAdaptor : public VKDataTypeSyncAdaptor, public QtContactsSqliteExtensions::TwoWayContactSyncAdapter
{
    Q_OBJECT

public:
    VKContactSyncAdaptor(QObject *parent);
   ~VKContactSyncAdaptor();

    QString syncServiceName() const;
    void sync(const QString &dataTypeString, int accountId = 0);

protected:
    // implementing the TWCSA interface
    void determineRemoteChanges(const QDateTime &remoteSince,
                                const QString &accountId);
    void upsyncLocalChanges(const QDateTime &localSince,
                            const QList<QContact> &locallyAdded,
                            const QList<QContact> &locallyModified,
                            const QList<QContact> &locallyDeleted,
                            const QString &accountId);

protected:
    // implementing VKDataTypeSyncAdaptor interface
    void purgeDataForOldAccounts(const QList<int> &oldIds);
    void beginSync(int accountId, const QString &accessToken);
    void finalize(int accountId);
    void finalCleanup();
    // implementing TWCSA interface
    bool testAccountProvenance(const QContact &contact, const QString &accountId);

private:
    void requestData(int accountId,
                     const QString &accessToken,
                     int startIndex = 0,
                     const QString &continuationRequest = QString(),
                     const QDateTime &syncTimestamp = QDateTime());
    void purgeAccount(int pid);

private Q_SLOTS:
    void contactsFinishedHandler();
    void imageDownloaded(const QString &url, const QString &path, const QVariantMap &metadata);

private:
    enum UpdateType {
        Add    = 1,
        Modify = 2,
        Remove = 3
    };
    QList<QContact> parseContacts(const QJsonArray &json, int accountId, const QString &accessToken, QString *continuationUrl);
    void continueSync(int accountId, const QString &accessToken);
    void upsyncLocalChangesList(int accountId);
    void queueOutstandingAvatars(int accountId, const QString &accessToken);
    bool queueAvatarForDownload(int accountId, const QString &accessToken, const QString &contactGuid, const QString &imageUrl);
    void transformContactAvatars(QList<QContact> &remoteContacts, int accountId, const QString &accessToken);
    void downloadContactAvatarImage(int accountId, const QString &accessToken, const QUrl &imageUrl, const QString &filename);
    bool readExtraStateData(int accountId);
    bool storeExtraStateData(int accountId);

private:
    QContactManager m_contactManager;
    VKContactImageDownloader *m_workerObject;

    QMap<int, QString> m_accessTokens;
    QMap<int, QList<QContact> > m_remoteContacts;
    QMap<int, QList<QContact> > m_remoteDels;
    QMap<int, QList<QContact> > m_remoteAddMods;
    QMap<int, QMap<QString, QString> > m_contactIds; // contact guid -> contact id
    QMap<int, QMap<QString, QString> > m_contactAvatars; // contact guid -> remote avatar path
    QMap<int, QList<QPair<QContact, UpdateType> > > m_localChanges; // currently unused.

    // the following are not preserved across sync runs via OOB.
    QMap<int, int> m_apiRequestsRemaining;
    QMap<int, QMap<QString, QString> > m_queuedAvatarsForDownload; // contact guid -> remote avatar path
    QMap<int, QMap<QString, QString> > m_downloadedContactAvatars; // contact guid -> local file path
};

#endif // VKCONTACTSYNCADAPTOR_H
