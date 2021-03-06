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

#ifndef GOOGLECALENDARSYNCADAPTOR_H
#define GOOGLECALENDARSYNCADAPTOR_H

#include "googledatatypesyncadaptor.h"

#include <QtCore/QString>
#include <QtCore/QMultiMap>
#include <QtCore/QPair>
#include <QtCore/QJsonObject>

#include <extendedcalendar.h>
#include <extendedstorage.h>
#include <icalformat.h>

#include <socialcache/googlecalendardatabase.h>

class GoogleCalendarSyncAdaptor : public GoogleDataTypeSyncAdaptor
{
    Q_OBJECT

public:
    GoogleCalendarSyncAdaptor(QObject *parent);
    ~GoogleCalendarSyncAdaptor();

    QString syncServiceName() const;
    void sync(const QString &dataTypeString, int accountId);

protected: // implementing GoogleDataTypeSyncAdaptor interface
    void purgeDataForOldAccount(int oldId, SocialNetworkSyncAdaptor::PurgeMode mode);
    void beginSync(int accountId, const QString &accessToken);
    void finalCleanup();

private:
    enum UpsyncType {
        UpsyncInsert = 1,
        UpsyncModify = 2,
        UpsyncDelete = 3
    };
    void requestCalendars(int accountId, const QString &accessToken,
                          bool needCleanSync, const QString &pageToken = QString());
    void requestEvents(int accountId, const QString &accessToken,
                       const QString &calendarId, bool needCleanSync,
                       const QString &pageToken = QString());
    void updateLocalCalendarNotebooks(int accountId, const QString &accessToken, bool needCleanSync);
    void updateLocalCalendarNotebookEvents(int accountId, const QString &accessToken,
                                           const QString &calendarId, const QDateTime &since);
    void upsyncChanges(int accountId, const QString &accessToken,
                       GoogleCalendarSyncAdaptor::UpsyncType upsyncType,
                       const QString &kcalEventId, const QString &calendarId,
                       const QString &eventId,const QByteArray &eventData);

private Q_SLOTS:
    void calendarsFinishedHandler();
    void eventsFinishedHandler();
    void upsyncFinishedHandler();

private:
    QMap<int, QMap<QString, QPair<QString, QString> > > m_serverCalendarIdToSummaryAndColor;
    QMap<int, QMultiMap<QString, QJsonObject> > m_calendarIdToEventObjects;
    QMap<int, bool> m_syncSucceeded;

    mKCal::ExtendedCalendar::Ptr m_calendar;
    mKCal::ExtendedStorage::Ptr m_storage;
    mutable KCalCore::ICalFormat m_icalFormat;
    bool m_storageNeedsSave;

    GoogleCalendarDatabase m_idDb; // solely for local-deletion-upsync support
};

#endif // GOOGLECALENDARSYNCADAPTOR_H
