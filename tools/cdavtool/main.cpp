/*
 * Copyright (C) 2016 Jolla Ltd.
 * Contact: Chris Adams <chris.adams@jollamobile.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#include <QCoreApplication>
#include <QStringList>
#include <QString>
#include <QFile>
#include <QBuffer>
#include <QDataStream>
#include <QtDebug>

#include <stdio.h>

#include "worker.h"

#define RETURN_SUCCESS 0
#define RETURN_ERROR 1

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    CDavToolWorker worker;
    QObject::connect(&worker, &CDavToolWorker::done, &app, &QCoreApplication::quit);
    const QString usage = QStringLiteral(
               "usage:\n"
               "cdavtool --create-account --type carddav|caldav|both --username <user> --password <pass> --host <host> [--calendar-path <cpath>] [--addressbook-path <apath>] [--verbose]\n"
               "cdavtool --with-account <id> [--clear-remote-calendars|--clear-remote-addressbooks] [--verbose]\n"
               "cdavtool --delete-account <id> [--verbose]\n"
               "\n"
               "examples:\n"
               "cdavtool --create-account --type both --username testuser --password testpass --host http://8.1.tst.merproject.org/ --verbose\n"
               "cdavtool --with-account 5 --clear-remote-calendars\n"
               "cdavtool --delete-account 5\n");

    QStringList args = app.arguments();
    if (args.last() == QStringLiteral("--verbose")) {
        args.removeLast();
        worker.setVerbose(true);
    }

    if (args.size() < 3 || args.size() > 14) {
        printf("%s\n", "Too few or many arguments.");
        printf("%s\n", usage.toLatin1().constData());
        return RETURN_ERROR;
    } else if (args[1] == QStringLiteral("--create-account")) {
        if (args.size() < 10
                || args[2] != QStringLiteral("--type")
                || args[4] != QStringLiteral("--username")
                || args[6] != QStringLiteral("--password")
                || args[8] != QStringLiteral("--host")) {
            printf("%s\n", "Incorrect switches for --create-account:");
            printf("%s\n", "Missing --type, --username, --password or --host arguments.");
            printf("%s\n", usage.toLatin1().constData());
            return RETURN_ERROR;
        }
        // parse all args.
        QString type = args[3], username = args[5], password = args[7], host = args[9];
        CDavToolWorker::CreateMode mode = CDavToolWorker::CreateBoth;
        if (type == QStringLiteral("carddav")) {
            mode = CDavToolWorker::CreateCardDAV;
        } else if (type == QStringLiteral("caldav")) {
            mode = CDavToolWorker::CreateCalDAV;
        }
        // create the account.
        if (args.size() == 10) {
            worker.createAccount(username, password, mode, host);
        } else if (args.size() == 12) {
            if (args[10] == QStringLiteral("--calendar-path")) {
                worker.createAccount(username, password, mode, host, args[11], QString());
            } else {
                worker.createAccount(username, password, mode, host, QString(), args[11]);
            }
        } else if (args.size() == 14) {
            if (args[10] == QStringLiteral("--calendar-path")) {
                worker.createAccount(username, password, mode, host, args[11], args[13]);
            } else {
                worker.createAccount(username, password, mode, host, args[13], args[11]);
            }
        } else {
            printf("%s\n", "Invalid switches for --create-account");
            printf("%s\n", usage.toLatin1().constData());
            return RETURN_ERROR;
        }
    } else if (args[1] == QStringLiteral("--with-account")) {
        if (args.size() != 4) {
            printf("%s\n", "Incorrect switches for --with-account");
            printf("%s\n", usage.toLatin1().constData());
            return RETURN_ERROR;
        }
        bool ok = false;
        int accountId = args[2].toInt(&ok);
        if (!ok || accountId <= 0) {
            printf("%s\n", "Invalid switches for --with-account (id)");
            printf("%s\n", usage.toLatin1().constData());
            return RETURN_ERROR;
        }
        if (args[3] == QStringLiteral("--clear-remote-calendars")) {
            worker.clearRemoteCalendars(accountId);
        } else if (args[3] == QStringLiteral("--clear-remote-addressbooks")) {
            worker.clearRemoteAddressbooks(accountId);
        } else {
            printf("%s\n", "Invalid switches for --with-account (method)");
            printf("%s\n", usage.toLatin1().constData());
            return RETURN_ERROR;
        }
    } else if (args[1] == QStringLiteral("--delete-account")) {
        if (args.size() != 3) {
            printf("%s\n", "Incorrect switches for --delete-account");
            printf("%s\n", usage.toLatin1().constData());
            return RETURN_ERROR;
        }
        bool ok = false;
        int accountId = args[2].toInt(&ok);
        if (!ok || accountId <= 0) {
            printf("%s\n", "Invalid switches for --delete-account (id)");
            printf("%s\n", usage.toLatin1().constData());
            return RETURN_ERROR;
        } else {
            worker.deleteAccount(accountId);
        }
    } else {
        printf("%s\n", "Invalid operation specified.");
        printf("%s\n", usage.toLatin1().constData());
        return RETURN_ERROR;
    }

    (void)app.exec();
    return worker.errorOccurred() ? RETURN_ERROR : RETURN_SUCCESS;
}
