/* Copyright (c) 2020-2025 hors<horsicq@gmail.com>
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
#include "xgithub.h"

XGithub::XGithub(const QString &sUserName, const QString &sRepoName, QObject *pParent) : QObject(pParent)
{
    this->m_sUserName = sUserName;
    this->m_sRepoName = sRepoName;

    m_bIsStop = false;
}

XGithub::~XGithub()
{
    m_bIsStop = true;

    QSetIterator<QNetworkReply *> i(m_stReplies);

    while (i.hasNext()) {
        QNetworkReply *pReply = i.next();

        pReply->abort();
    }
    // TODO Check
    // TODO wait
}

XGithub::RELEASE_HEADER XGithub::getTagRelease(QString sTag)
{
    QString sURL = QString("https://api.github.com/repos/%1/%2/releases/tags/%3").arg(m_sUserName, m_sRepoName, sTag);

    return _getRelease(sURL);
}

XGithub::RELEASE_HEADER XGithub::getLatestRelease(bool bPrerelease)
{
    QString sURL;

    if (!bPrerelease) {
        sURL = QString("https://api.github.com/repos/%1/%2/releases/latest").arg(m_sUserName, m_sRepoName);
    } else {
        sURL = QString("https://api.github.com/repos/%1/%2/releases").arg(m_sUserName, m_sRepoName);
    }

    return _getRelease(sURL);
}

QList<QString> XGithub::getDownloadLinks(QString sString)
{
    QList<QString> listResult;

    qint32 nCount = sString.count("](");

    for (qint32 i = 0; i < nCount; i++) {
        QString sLink = sString.section("](", i + 1, i + 1);
        sLink = sLink.section(")", 0, 0);

        listResult.append(sLink);
    }

    return listResult;
}

XGithub::RELEASE_HEADER XGithub::_handleReleaseJson(QJsonObject jsonObject)
{
    RELEASE_HEADER result = {};

    result.bValid = true;
    result.sName = jsonObject["name"].toString();
    result.sTag = jsonObject["tag_name"].toString();
    result.sBody = jsonObject["body"].toString();
    result.dt = QDateTime::fromString(jsonObject["published_at"].toString(), "yyyy-MM-ddThh:mm:ssZ");

    QJsonArray jsonArray = jsonObject["assets"].toArray();

    qint32 nCount = jsonArray.count();

    for (qint32 i = 0; i < nCount; i++) {
        RELEASE_RECORD record = {};

        QJsonObject _object = jsonArray.at(i).toObject();

        record.sSrc = _object["browser_download_url"].toString();
        record.nSize = _object["size"].toInt();
        record.sName = _object["name"].toString();
        record.dt = QDateTime::fromString(_object["updated_at"].toString(), "yyyy-MM-ddThh:mm:ssZ");

        result.listRecords.append(record);
    }

    return result;
}

void XGithub::setCredentials(QString sUser, QString sToken)
{
    m_sAuthUser = sUser;
    m_sAuthToken = sToken;
}

XGithub::WEBFILE XGithub::getWebFile(const QString &sUrl)
{
    WEBFILE result = {};

    QNetworkAccessManager nam;
    QUrl url(sUrl);
    QNetworkRequest req(url);
    QNetworkReply *pReply = nam.get(req);
    QEventLoop loop;
    QObject::connect(pReply, SIGNAL(finished()), &loop, SLOT(quit()));

    if (!(pReply->isFinished())) {
        loop.exec();
    }

    if (pReply->error() == QNetworkReply::NoError) {
        if (pReply->bytesAvailable()) {
            result.sContent = pReply->readAll();
            result.bValid = true;
        } else {
            QString sRedirectUrl = pReply->attribute(QNetworkRequest::RedirectionTargetAttribute).toString();

            if (sRedirectUrl != "") {
                result = getWebFile(sRedirectUrl);
                result.bRedirect = true;
                result.sRedirectUrl = sRedirectUrl;
            }
        }
    } else {
        result.bValid = false;
        result.sNetworkError = pReply->errorString();
    }

    return result;
}

XGithub::RELEASE_HEADER XGithub::_getRelease(const QString &sUrl)
{
    XGithub::RELEASE_HEADER result = {};

    QNetworkRequest req;
    req.setUrl(QUrl(QString(sUrl)));

    // Add credentials if supplied
    if (!m_sAuthUser.isEmpty()) {
        QString auth = m_sAuthUser + ":" + m_sAuthToken;
        auth = "Basic " + auth.toLocal8Bit().toBase64();
        req.setRawHeader("Authorization", auth.toLocal8Bit());
    }

    QNetworkReply *pReply = m_naManager.get(req);

    m_stReplies.insert(pReply);

    QEventLoop loop;
    QObject::connect(pReply, SIGNAL(finished()), &loop, SLOT(quit()));
    loop.exec();

    if (!m_bIsStop) {
        if (!pReply->error()) {
            QByteArray baData = pReply->readAll();
            QJsonDocument document = QJsonDocument::fromJson(baData);

#ifdef QT_DEBUG
            QString strJson(document.toJson(QJsonDocument::Indented));
            qDebug(strJson.toLatin1().data());
#endif

            if (document.isArray()) {
                QJsonArray jsArray = document.array();

                if (jsArray.count()) {
                    result = _handleReleaseJson(jsArray.at(0).toObject());
                }
            } else {
                result = _handleReleaseJson(document.object());
            }
        } else {
            QString sErrorString = pReply->errorString();

            if (sErrorString.contains("server replied: rate limit exceeded")) {
                sErrorString += "\n";
                sErrorString += "Github has the limit is 60 requests per hour for unauthenticated users (and 5000 for authenticated users).";
                sErrorString += "\n";
                sErrorString += "\n";
                sErrorString += "TRY AGAIN IN ONE HOUR!";
            }

            emit errorMessage(sErrorString);

#ifdef QT_DEBUG
            qDebug(sErrorString.toLatin1().data());
#endif

            result.bNetworkError = true;
        }
    }

    m_stReplies.remove(pReply);

    return result;
}
