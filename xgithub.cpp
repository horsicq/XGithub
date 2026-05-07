/* Copyright (c) 2020-2026 hors<horsicq@gmail.com>
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

#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {
constexpr int XGITHUB_TIMEOUT_MS = 30000;
const QString XGITHUB_USER_AGENT = QStringLiteral("XGitHub/1.0");
const QString XGITHUB_ACCEPT_JSON = QStringLiteral("application/vnd.github+json");
const QString XGITHUB_ACCEPT_STREAM = QStringLiteral("application/octet-stream");

QString findCurlProgram()
{
    QString sProgram = QStandardPaths::findExecutable(QStringLiteral("curl.exe"));

    if (sProgram.isEmpty()) {
        sProgram = QStandardPaths::findExecutable(QStringLiteral("curl"));
    }

    return sProgram;
}

bool executeCurl(const QString &sProgram, const QStringList &listArguments, QByteArray *pStdOut, QString *pError)
{
    QProcess process;
    process.start(sProgram, listArguments);

    if (!process.waitForStarted(5000)) {
        if (pError) {
            *pError = QStringLiteral("Cannot start curl: %1").arg(process.errorString());
        }

        return false;
    }

    if (!process.waitForFinished(35000)) {
        process.kill();
        process.waitForFinished();

        if (pError) {
            *pError = QStringLiteral("curl timeout");
        }

        return false;
    }

    QByteArray baStdOut = process.readAllStandardOutput();
    QByteArray baStdErr = process.readAllStandardError();

    if (pStdOut) {
        *pStdOut = baStdOut;
    }

    if ((process.exitStatus() != QProcess::NormalExit) || (process.exitCode() != 0)) {
        if (pError) {
            *pError = QString::fromLocal8Bit(baStdErr).trimmed();

            if (pError->isEmpty()) {
                *pError = QStringLiteral("curl failed with exit code %1").arg(process.exitCode());
            }
        }

        return false;
    }

    return true;
}

QByteArray buildBasicAuthHeader(const QString &sUser, const QString &sToken)
{
    if (sUser.isEmpty()) {
        return {};
    }

    return QByteArrayLiteral("Basic ")
           + QStringLiteral("%1:%2")
                 .arg(sUser, sToken)
                 .toLocal8Bit()
                 .toBase64();
}

bool requestCurl(const QString &sCurlProgram, const QString &sUrl, const QString &sAcceptHeader, QByteArray *pStdOut, QString *pError,
                 const QString &sAuthUser = QString(), const QString &sAuthToken = QString(), const QString &sOutputFile = QString())
{
    if (sCurlProgram.isEmpty()) {
        return false;
    }

    QStringList listArguments;

    listArguments << QStringLiteral("--silent") << QStringLiteral("--show-error") << QStringLiteral("--location") << QStringLiteral("--fail")
                  << QStringLiteral("--max-time") << QString::number(XGITHUB_TIMEOUT_MS / 1000) << QStringLiteral("--user-agent") << XGITHUB_USER_AGENT
                  << QStringLiteral("--header") << QStringLiteral("Accept: %1").arg(sAcceptHeader);

    if (!sAuthUser.isEmpty()) {
        listArguments << QStringLiteral("--user") << QStringLiteral("%1:%2").arg(sAuthUser, sAuthToken);
    }

    if (!sOutputFile.isEmpty()) {
        listArguments << QStringLiteral("--output") << sOutputFile;
    }

    listArguments << sUrl;

    return executeCurl(sCurlProgram, listArguments, pStdOut, pError);
}

bool requestNetwork(const QString &sUrl, const QString &sAcceptHeader, QByteArray *pData, QString *pError, const QString &sAuthUser = QString(),
                   const QString &sAuthToken = QString(), QString *pRedirectUrl = nullptr)
{
    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest req{QUrl(sUrl)};
    req.setRawHeader("User-Agent", XGITHUB_USER_AGENT.toLatin1());
    req.setRawHeader("Accept", sAcceptHeader.toLatin1());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(XGITHUB_TIMEOUT_MS);

    QByteArray baAuth = buildBasicAuthHeader(sAuthUser, sAuthToken);
    if (!baAuth.isEmpty()) {
        req.setRawHeader("Authorization", baAuth);
    }

    QNetworkReply *pReply = nam.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(pReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, pReply, &QNetworkReply::abort);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(XGITHUB_TIMEOUT_MS);

    if (!(pReply->isFinished())) {
        loop.exec();
    }

    timer.stop();

    bool bResult = (pReply->error() == QNetworkReply::NoError);

    if (bResult) {
        if (pData) {
            *pData = pReply->readAll();
        }

        if (pRedirectUrl) {
            *pRedirectUrl = pReply->attribute(QNetworkRequest::RedirectionTargetAttribute).toString();
        }
    } else if (pError) {
        if (pReply->error() == QNetworkReply::OperationCanceledError) {
            *pError = QStringLiteral("Request timeout: %1").arg(sUrl);
        } else {
            *pError = pReply->errorString();
        }
    }

    pReply->deleteLater();

    return bResult;
}

}  // namespace

XGitHub::XGitHub(const QString &sUserName, const QString &sRepoName, QObject *pParent) : QObject(pParent), m_sUserName(sUserName), m_sRepoName(sRepoName)
{
}

XGitHub::~XGitHub()
{
}

XGitHub::RELEASE_HEADER XGitHub::getTagRelease(const QString &sTag)
{
    QString sURL = QString("https://api.github.com/repos/%1/%2/releases/tags/%3").arg(m_sUserName, m_sRepoName, sTag);

    return _getRelease(sURL);
}

XGitHub::RELEASE_HEADER XGitHub::getLatestRelease(bool bPrerelease)
{
    QString sURL;

    if (!bPrerelease) {
        sURL = QString("https://api.github.com/repos/%1/%2/releases/latest").arg(m_sUserName, m_sRepoName);
    } else {
        sURL = QString("https://api.github.com/repos/%1/%2/releases").arg(m_sUserName, m_sRepoName);
    }

    return _getRelease(sURL);
}

QList<QString> XGitHub::getDownloadLinks(const QString &sString)
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

XGitHub::RELEASE_HEADER XGitHub::_handleReleaseJson(const QJsonObject &jsonObject)
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

void XGitHub::setCredentials(const QString &sUser, const QString &sToken)
{
    m_sAuthUser = sUser;
    m_sAuthToken = sToken;
}

XGitHub::WEBFILE XGitHub::getWebFile(const QString &sUrl)
{
    WEBFILE result = {};
    QString sCurlProgram = findCurlProgram();
    QString sError;
    QByteArray baData;

    if (!sCurlProgram.isEmpty()) {
        if (requestCurl(sCurlProgram, sUrl, XGITHUB_ACCEPT_JSON, &baData, &sError)) {
            result.sContent = QString::fromUtf8(baData);
            result.bValid = true;
            return result;
        }

        result.sNetworkError = sError;
        return result;
    }

    QString sRedirectUrl;
    if (requestNetwork(sUrl, XGITHUB_ACCEPT_JSON, &baData, &sError, QString(), QString(), &sRedirectUrl)) {
        if (!baData.isEmpty()) {
            result.sContent = QString::fromUtf8(baData);
            result.bValid = true;
            return result;
        }

        if (!sRedirectUrl.isEmpty()) {
            result = getWebFile(sRedirectUrl);
            result.bRedirect = true;
            result.sRedirectUrl = sRedirectUrl;
            return result;
        }
    }

    result.sNetworkError = sError;

    return result;
}

bool XGitHub::downloadFile(const QString &sUrl, const QString &sLocalFilePath)
{
    bool bResult = false;
    QString sCurlProgram = findCurlProgram();
    QString sError;
    QByteArray baData;

    if (!sCurlProgram.isEmpty()) {
        bResult = requestCurl(sCurlProgram, sUrl, XGITHUB_ACCEPT_STREAM, nullptr, &sError, QString(), QString(), sLocalFilePath);

        if ((!bResult) && QFile::exists(sLocalFilePath)) {
            QFile::remove(sLocalFilePath);
        }

        return bResult;
    }

    if (requestNetwork(sUrl, XGITHUB_ACCEPT_STREAM, &baData, &sError)) {
        QFile file(sLocalFilePath);

        if (file.open(QIODevice::WriteOnly)) {
            file.write(baData);
            file.close();
            bResult = true;
        }
    }

    return bResult;
}

XGitHub::RELEASE_HEADER XGitHub::_getRelease(const QString &sUrl)
{
    XGitHub::RELEASE_HEADER result = {};
    QString sCurlProgram = findCurlProgram();
    QString sError;
    QByteArray baData;

    if (!sCurlProgram.isEmpty()) {
        if (requestCurl(sCurlProgram, sUrl, XGITHUB_ACCEPT_JSON, &baData, &sError, m_sAuthUser, m_sAuthToken)) {
            QJsonDocument document = QJsonDocument::fromJson(baData);

            if (document.isArray()) {
                QJsonArray jsArray = document.array();

                if (!jsArray.isEmpty()) {
                    result = _handleReleaseJson(jsArray.at(0).toObject());
                }
            } else {
                result = _handleReleaseJson(document.object());
            }

            return result;
        }

        emit errorMessage(sError);
        result.bNetworkError = true;

        return result;
    }

    if (requestNetwork(sUrl, XGITHUB_ACCEPT_JSON, &baData, &sError, m_sAuthUser, m_sAuthToken)) {
        QJsonDocument document = QJsonDocument::fromJson(baData);

#ifdef QT_DEBUG
        QString strJson(document.toJson(QJsonDocument::Indented));
        qDebug(strJson.toLatin1().data());
#endif

        if (document.isArray()) {
            QJsonArray jsArray = document.array();

            if (!jsArray.isEmpty()) {
                result = _handleReleaseJson(jsArray.at(0).toObject());
            }
        } else {
            result = _handleReleaseJson(document.object());
        }
    } else {
        QString sErrorString = sError;

        if (sErrorString.contains("server replied: rate limit exceeded\n")) {
            sErrorString += QStringLiteral("Github has the limit is 60 requests per hour for unauthenticated users (and 5000 for authenticated users).\n\n");
            sErrorString += "TRY AGAIN IN ONE HOUR!";
        }

        emit errorMessage(sErrorString);

#ifdef QT_DEBUG
        qDebug(sErrorString.toLatin1().data());
#endif

        result.bNetworkError = true;
    }

    return result;
}
