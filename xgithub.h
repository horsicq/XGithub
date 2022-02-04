/* Copyright (c) 2020-2022 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef XGITHUB_H
#define XGITHUB_H

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class XGithub : public QObject
{
    Q_OBJECT

public:
    struct RELEASE_RECORD
    {
        QString sName;
        QString sSrc;
        qint64 nSize;
        QDateTime dt;
    };

    struct RELEASE_HEADER
    {
        bool bValid;
        QString sName;
        QString sTag;
        QString sBody;
        QDateTime dt;
        QList<RELEASE_RECORD> listRecords;
    };

    explicit XGithub(QString sUserName,QString sRepoName,QObject *pParent=nullptr);
    ~XGithub();

    RELEASE_HEADER getLatestRelease(bool bPrerelease);
    static QList<QString> getDownloadLinks(QString sString);
    void setCredentials(QString sUser,QString sToken);

private:
    RELEASE_HEADER getRelease(QJsonObject jsonObject);

signals:
    void errorMessage(QString sText);

private:
    QString sUserName;
    QString sRepoName;
    QString sAuthUser;
    QString sAuthToken;
    bool bIsStop;
    QSet<QNetworkReply *> stReplies;
    QNetworkAccessManager naManager;
};

#endif // XGITHUB_H
