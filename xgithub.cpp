// Copyright (c) 2020-2021 hors<horsicq@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
#include "xgithub.h"

XGithub::XGithub(QString sUserName, QString sRepoName, QObject *pParent) : QObject(pParent)
{
    this->sUserName=sUserName;
    this->sRepoName=sRepoName;

    bIsStop=false;
}

XGithub::~XGithub()
{
    bIsStop=true;

    QSetIterator<QNetworkReply *> i(stReplies);
    while(i.hasNext())
    {
        QNetworkReply *pReply=i.next();

        pReply->abort();
    }

    // TODO wait
}

XGithub::RELEASE_HEADER XGithub::getLatestRelease(bool bPrerelease)
{
    RELEASE_HEADER result={};

    QNetworkRequest req;

    if(!bPrerelease)
    {
        req.setUrl(QUrl(QString("https://api.github.com/repos/%1/%2/releases/latest").arg(sUserName).arg(sRepoName)));
    }
    else
    {
        req.setUrl(QUrl(QString("https://api.github.com/repos/%1/%2/releases").arg(sUserName).arg(sRepoName)));
    }

    // Add credentials if supplied
    if(!sAuthUser.isEmpty())
    {
        QString auth = sAuthUser + ":" + sAuthToken;
        auth = "Basic " + auth.toLocal8Bit().toBase64();
        req.setRawHeader("Authorization", auth.toLocal8Bit());
    }

    QNetworkReply *pReply=naManager.get(req);

    stReplies.insert(pReply);

    QEventLoop loop;
    QObject::connect(pReply,SIGNAL(finished()),&loop,SLOT(quit()));
    loop.exec();

    if(!bIsStop)
    {
        if(!pReply->error())
        {
            QByteArray baData=pReply->readAll();
            QJsonDocument document=QJsonDocument::fromJson(baData);

        #ifdef QT_DEBUG
            QString strJson(document.toJson(QJsonDocument::Indented));
            qDebug(strJson.toLatin1().data());
        #endif

            if(!bPrerelease)
            {
                result=getRelease(document.object());
            }
            else
            {
                QJsonArray jsArray=document.array();

                if(jsArray.count())
                {
                    result=getRelease(jsArray.at(0).toObject());
                }
            }
        }
        else
        {
            QString sErrorString=pReply->errorString();

            if(sErrorString.contains("server replied: rate limit exceeded"))
            {
                sErrorString+="\n";
                sErrorString+="Github has the limit is 60 requests per hour for unauthenticated users (and 5000 for authenticated users).";
                sErrorString+="\n";
                sErrorString+="\n";
                sErrorString+="TRY AGAIN IN ONE HOUR!";
            }

            emit errorMessage(sErrorString);
        }
    }

    stReplies.remove(pReply);

    return result;
}

QList<QString> XGithub::getDownloadLinks(QString sString)
{
    QList<QString> listResult;

    int nCount=sString.count("](");

    for(int i=0;i<nCount;i++)
    {
        QString sLink=sString.section("](",i+1,i+1);
        sLink=sLink.section(")",0,0);

        listResult.append(sLink);
    }

    return listResult;
}

XGithub::RELEASE_HEADER XGithub::getRelease(QJsonObject jsonObject)
{
    RELEASE_HEADER result={};

    result.bValid=true;
    result.sName=jsonObject["name"].toString();
    result.sTag=jsonObject["tag_name"].toString();
    result.sBody=jsonObject["body"].toString();
    result.dt=QDateTime::fromString(jsonObject["published_at"].toString(),"yyyy-MM-ddThh:mm:ssZ");

    QJsonArray jsonArray=jsonObject["assets"].toArray();

    int nCount=jsonArray.count();

    for(int i=0;i<nCount;i++)
    {
        RELEASE_RECORD record={};

        QJsonObject _object=jsonArray.at(i).toObject();

        record.sSrc=_object["browser_download_url"].toString();
        record.nSize=_object["size"].toInt();
        record.sName=_object["name"].toString();
        record.dt=QDateTime::fromString(_object["updated_at"].toString(),"yyyy-MM-ddThh:mm:ssZ");

        result.listRecords.append(record);
    }

    return result;
}

void XGithub::setCredentials(QString sUser, QString sToken)
{
    sAuthUser = sUser;
    sAuthToken = sToken;
}
