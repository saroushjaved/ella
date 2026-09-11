#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QFile>
#include <QtCore/private/qzipwriter_p.h>
#include <QtCore/private/qzipreader_p.h>
#include "core/AppConfig.h"
#include "core/WorkspaceService.h"
#include "database/DatabaseManager.h"
#include "models/FileListModel.h"
#include "notes/NoteManager.h"
#include "search/ContentExtractor.h"

class WorkspaceTests: public QObject {
    Q_OBJECT
    QTemporaryDir data;
private slots:
    void initTestCase() {
        QVERIFY(data.isValid()); qputenv("ELLA_APP_DATA_DIR",data.path().toUtf8());
        QVERIFY(DatabaseManager::instance().initialize());
    }
    void officeExtractionPreservesSourceLocations() {
        const QString docx=data.filePath("research.docx");
        { QZipWriter zip(docx); zip.addFile("word/document.xml",QByteArray("<w:document xmlns:w='urn:word'><w:body><w:p><w:r><w:t>Control systems</w:t></w:r></w:p><w:p><w:r><w:t>Laplace transform</w:t></w:r></w:p></w:body></w:document>")); zip.close(); }
        ContentExtractor extractor; auto result=extractor.extract(docx,"","docx");
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QCOMPARE(result.passages.size(),2);
        QCOMPARE(result.passages[1].toMap()["pageNumber"].toInt(),2);
        QVERIFY(result.text.contains("Laplace"));
        const QString pptx=data.filePath("slides.pptx");
        { QZipWriter zip(pptx); zip.addFile("ppt/slides/slide2.xml",QByteArray("<a:p xmlns:a='urn:drawing'><a:t>Second slide</a:t></a:p>")); zip.addFile("ppt/slides/slide1.xml",QByteArray("<a:p xmlns:a='urn:drawing'><a:t>First slide</a:t></a:p>")); zip.close(); }
        result=extractor.extract(pptx,"","pptx"); QCOMPARE(result.passages.size(),2);
        QCOMPARE(result.passages.first().toMap()["text"].toString(),QString("First slide"));
        QCOMPARE(result.passages.last().toMap()["anchorType"].toString(),QString("slide"));
    }
    void malformedOfficeFailsWithoutPartialContent() {
        const QString path=data.filePath("bad.docx");
        {QZipWriter zip(path); zip.addFile("word/document.xml",QByteArray("<document><p>broken")); zip.close();}
        const auto result=ContentExtractor().extract(path,"","docx"); QVERIFY(!result.error.isEmpty()); QVERIFY(result.text.isEmpty());
    }
    void pdfPassagesKeepPageNumbers() {
        const QString path=data.filePath("paper.pdf");
        QByteArray pdf("%PDF-1.4\n");
        QVector<qsizetype> offsets(8);
        const auto addObject=[&](int number,const QByteArray& body) {
            offsets[number]=pdf.size();
            pdf+=QByteArray::number(number)+" 0 obj\n"+body+"\nendobj\n";
        };
        addObject(1,"<< /Type /Catalog /Pages 2 0 R >>");
        addObject(2,"<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>");
        addObject(3,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 7 0 R >> >> /Contents 4 0 R >>");
        const QByteArray first="BT /F1 18 Tf 72 720 Td (First page memory) Tj ET";
        addObject(4,"<< /Length "+QByteArray::number(first.size())+" >>\nstream\n"+first+"\nendstream");
        addObject(5,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 7 0 R >> >> /Contents 6 0 R >>");
        const QByteArray second="BT /F1 18 Tf 72 720 Td (Second page recall) Tj ET";
        addObject(6,"<< /Length "+QByteArray::number(second.size())+" >>\nstream\n"+second+"\nendstream");
        addObject(7,"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
        const qsizetype xref=pdf.size();
        pdf+="xref\n0 8\n0000000000 65535 f \n";
        for(int i=1;i<8;++i) pdf+=QByteArray::number(offsets[i]).rightJustified(10,'0')+" 00000 n \n";
        pdf+="trailer\n<< /Size 8 /Root 1 0 R >>\nstartxref\n"+QByteArray::number(xref)+"\n%%EOF\n";
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(pdf),pdf.size()); file.close();
        auto result=ContentExtractor().extract(path,"application/pdf","pdf");
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QCOMPARE(result.passages.size(),2);
        QCOMPARE(result.passages.last().toMap()["pageNumber"].toInt(),2);
    }
    void notesBackupAndRestoreAreSourceSafe() {
        NoteManager notes; auto note=notes.createNote("Research","","","","","<p>Original insight</p>"); QVERIFY(!note.isEmpty());
        const int id=note["id"].toInt(); QVERIFY(notes.saveNote(id,"Research","","","","","<p>Updated insight</p>"));
        const QString original=note["path"].toString();
        WorkspaceService service; QSignalSpy completed(&service,&WorkspaceService::operationFinished);
        const QString archive=data.filePath("library.ella-backup"); QVERIFY(service.backupLibrary(archive)["queued"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(),1,15000); QVERIFY2(completed.takeFirst()[0].toMap()["ok"].toBool(),qPrintable(service.status()));
        QZipReader zip(archive); QVERIFY(zip.fileData("notes/"+QString::number(id)+".md").contains("Updated insight"));
        QVERIFY(!zip.fileData("library.sqlite").isEmpty());
        QVERIFY(service.restoreLibrary(archive)["queued"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(),1,15000); auto restored=completed.takeFirst()[0].toMap();
        QVERIFY2(restored["ok"].toBool(),qPrintable(restored["error"].toString())); QVERIFY(restored["restartRequired"].toBool());
        QVERIFY(QFile::exists(original)); QVERIFY(QFile::exists(AppConfig::databasePath()));
    }
    void invalidRestoreLeavesLibraryUntouched() {
        const QString invalid=data.filePath("invalid.zip");
        {QZipWriter zip(invalid); zip.addFile("../escape",QByteArray("bad")); zip.close();}
        WorkspaceService service; QSignalSpy completed(&service,&WorkspaceService::operationFinished);
        service.restoreLibrary(invalid); QTRY_COMPARE_WITH_TIMEOUT(completed.size(),1,5000);
        QVERIFY(!completed.first()[0].toMap()["ok"].toBool()); QVERIFY(QFile::exists(AppConfig::databasePath()));
    }
    void sourceNotesHaveBacklinks() {
        const QString path=data.filePath("source.txt"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("Quoted text"); file.close();
        FileListModel model; model.importFiles({path}); QSqlQuery q(DatabaseManager::instance().database()); q.prepare("SELECT id FROM files WHERE path=?"); q.addBindValue(path); QVERIFY(q.exec()); QVERIFY(q.next());
        const int id=q.value(0).toInt(); WorkspaceService service; auto note=service.createSourceNote(id,"Insight","Quoted text",{{"locator","Paragraph 1"}});
        QVERIFY(note["ok"].toBool()); QCOMPARE(service.backlinks(id).size(),1);
    }
};
QTEST_MAIN(WorkspaceTests)
#include "WorkspaceTests.moc"
