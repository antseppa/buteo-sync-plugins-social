/****************************************************************************
 **
 ** Copyright (C) 2014 Jolla Ltd.
 ** Contact: Chris Adams <chris.adams@jolla.com>
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

#include "facebooknotificationsplugin.h"
#include "facebooknotificationsyncadaptor.h"
#include "socialnetworksyncadaptor.h"

extern "C" FacebookNotificationsPlugin* createPlugin(const QString& pluginName,
                                       const Buteo::SyncProfile& profile,
                                       Buteo::PluginCbInterface *callbackInterface)
{
    return new FacebookNotificationsPlugin(pluginName, profile, callbackInterface);
}

extern "C" void destroyPlugin(FacebookNotificationsPlugin* plugin)
{
    delete plugin;
}

FacebookNotificationsPlugin::FacebookNotificationsPlugin(const QString& pluginName,
                             const Buteo::SyncProfile& profile,
                             Buteo::PluginCbInterface *callbackInterface)
    : SocialdButeoPlugin(pluginName, profile, callbackInterface,
                         QStringLiteral("facebook"),
                         SocialNetworkSyncAdaptor::dataTypeName(SocialNetworkSyncAdaptor::Notifications))
{
}

FacebookNotificationsPlugin::~FacebookNotificationsPlugin()
{
}

SocialNetworkSyncAdaptor *FacebookNotificationsPlugin::createSocialNetworkSyncAdaptor()
{
    return new FacebookNotificationSyncAdaptor(this);
}
