// Copyright (c) 2020 hors<horsicq@gmail.com>
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

XGithub::RELEASE_HEADER XGithub::getLatestRelease()
{
    RELEASE_HEADER result={};

    QNetworkRequest req(QUrl(QString("https://api.github.com/repos/%1/%2/releases/latest").arg(sUserName).arg(sRepoName)));
    QNetworkReply *pReply=nam.get(req);

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

            QString strJson(document.toJson(QJsonDocument::Indented));

            qDebug(strJson.toLatin1().data());
        }
        else
        {
            QString sErrorString=pReply->errorString();

            emit errorMessage(sErrorString);
        }
    }

    stReplies.remove(pReply);

    return result;
}
