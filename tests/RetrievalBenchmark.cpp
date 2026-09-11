#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdio>

#include "core/AppConfig.h"
#include "database/DatabaseManager.h"
#include "library/FileRepository.h"

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);QTemporaryDir profile;if(!profile.isValid())return 2;
    qputenv("ELLA_APP_DATA_DIR",profile.path().toUtf8());if(!DatabaseManager::instance().initialize())return 3;
    QSqlDatabase db=DatabaseManager::instance().database();if(!db.transaction())return 4;
    QSqlQuery file(db),passage(db),fts(db);
    file.prepare("INSERT INTO files(path,name,extension,mime_type,size_bytes,indexed_at,status,source,document_type) VALUES(?,?,'.txt','text/plain',100,datetime('now'),0,'Benchmark','Text')");
    passage.prepare("INSERT INTO passages(file_id,ordinal,text,anchor_type,char_start,char_end,locator) VALUES(?,?,?,'text',0,100,?)");
    fts.prepare("INSERT INTO passage_fts(passage_id,file_id,content_text) VALUES(?,?,?)");
    for(int document=0;document<25000;++document){file.bindValue(0,QStringLiteral("C:/benchmark/%1.txt").arg(document,5,10,QChar('0')));file.bindValue(1,QStringLiteral("Document %1").arg(document));if(!file.exec()){fprintf(stderr,"%s\n",qPrintable(file.lastError().text()));return 5;}const qlonglong id=file.lastInsertId().toLongLong();for(int ordinal=0;ordinal<10;++ordinal){QString text=QStringLiteral("research passage %1 %2 retrieval context").arg(document).arg(ordinal);if(document<100&&ordinal==0)text+=QStringLiteral(" exactneedle%1").arg(document,3,10,QChar('0'));passage.bindValue(0,id);passage.bindValue(1,ordinal);passage.bindValue(2,text);passage.bindValue(3,QStringLiteral("Text %1").arg(ordinal*100));if(!passage.exec())return 6;fts.bindValue(0,passage.lastInsertId());fts.bindValue(1,id);fts.bindValue(2,text);if(!fts.exec())return 7;}}
    if(!db.commit())return 8;FileRepository repository;QVector<qint64> durations;int topFive=0;
    for(int queryIndex=0;queryIndex<100;++queryIndex){QElapsedTimer timer;timer.start();int total=0;const auto results=repository.queryFiles(QStringLiteral("exactneedle%1").arg(queryIndex,3,10,QChar('0')),-1,{},{},{},{},{},{},{},{},{} ,"name",true,5,0,&total);durations<<timer.nsecsElapsed()/1000000;for(const auto& result:results)if(result.id==queryIndex+1){++topFive;break;}}
    std::sort(durations.begin(),durations.end());const qint64 p95=durations.at(94);const QJsonObject report{{"documents",25000},{"passages",250000},{"queries",100},{"keywordTopFivePercent",topFive},{"warmP95Ms",p95},{"targetMs",300}};
    const QByteArray output=QJsonDocument(report).toJson(QJsonDocument::Indented);fwrite(output.constData(),1,size_t(output.size()),stdout);return topFive>=90&&p95<300?0:9;
}
