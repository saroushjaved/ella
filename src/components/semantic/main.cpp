#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QThread>
#include <array>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <hnswlib/hnswlib.h>

namespace {
constexpr int kMaxTokens=512;
QString argument(const QStringList& args,const QString& name) { const int i=args.indexOf(name); return i>=0&&i+1<args.size()?args[i+1]:QString(); }
QString sha256(const QString& path) { QFile f(path); QCryptographicHash h(QCryptographicHash::Sha256); if(!f.open(QIODevice::ReadOnly)||!h.addData(&f))return{};return QString::fromLatin1(h.result().toHex()); }
void respond(const QJsonObject& value) { QTextStream out(stdout);out<<QJsonDocument(value).toJson(QJsonDocument::Compact)<<Qt::endl; }

class Encoder {
public:
    Encoder(const QString& model,const QString& vocab):m_model(model),m_hash(sha256(model)),m_env(ORT_LOGGING_LEVEL_WARNING,"ella-semantic"),m_session(nullptr) {
        QFile file(vocab); if(!file.open(QIODevice::ReadOnly|QIODevice::Text))throw std::runtime_error("Cannot read E5 vocabulary");
        int id=0; while(!file.atEnd())m_vocab.insert(QString::fromUtf8(file.readLine()).trimmed(),id++);
        if(!m_vocab.contains("[CLS]")||!m_vocab.contains("[SEP]")||!m_vocab.contains("[PAD]")||!m_vocab.contains("[UNK]"))throw std::runtime_error("Vocabulary is incomplete");
        Ort::SessionOptions options;options.SetIntraOpNumThreads(qMax(1,QThread::idealThreadCount()/2));options.SetInterOpNumThreads(1);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef Q_OS_WIN
        m_session=Ort::Session(m_env,reinterpret_cast<const wchar_t*>(model.utf16()),options);
#else
        m_session=Ort::Session(m_env,model.toUtf8().constData(),options);
#endif
        Ort::AllocatorWithDefaultOptions allocator; const size_t outputs=m_session.GetOutputCount(); if(outputs<1)throw std::runtime_error("Model has no output");
        auto shape=m_session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape(); if(shape.size()!=3||shape.back()<=0)throw std::runtime_error("Expected token embedding output"); m_dimension=int(shape.back());
        for(size_t i=0;i<m_session.GetInputCount();++i){auto name=m_session.GetInputNameAllocated(i,allocator);m_inputNames.emplace_back(name.get());}
        auto name=m_session.GetOutputNameAllocated(0,allocator);m_outputName=name.get();
    }
    int dimension()const{return m_dimension;} QString hash()const{return m_hash;}
    std::vector<float> encode(const QString& input) {
        QString normalized=input.normalized(QString::NormalizationForm_KC).toLower();
        const QStringList words=normalized.split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")),Qt::SkipEmptyParts);
        std::vector<qint64> ids{m_vocab["[CLS]"]};
        for(const QString& word:words){if(ids.size()>=kMaxTokens-1)break;appendWordPiece(word,ids);}
        ids.push_back(m_vocab["[SEP]"]); const qsizetype meaningful=ids.size(); ids.resize(kMaxTokens,m_vocab["[PAD]"]);
        std::vector<qint64> mask(kMaxTokens,0),types(kMaxTokens,0);std::fill(mask.begin(),mask.begin()+meaningful,1);
        const std::array<int64_t,2> shape{1,kMaxTokens}; auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        auto tensor=[&](std::vector<qint64>& values){return Ort::Value::CreateTensor<qint64>(memory,values.data(),values.size(),shape.data(),shape.size());};
        std::vector<Ort::Value> inputs;std::vector<const char*> names;
        for(const std::string& name:m_inputNames){names.push_back(name.c_str());if(name.find("attention")!=std::string::npos)inputs.emplace_back(tensor(mask));else if(name.find("token_type")!=std::string::npos)inputs.emplace_back(tensor(types));else inputs.emplace_back(tensor(ids));}
        const char* output=m_outputName.c_str();auto result=m_session.Run(Ort::RunOptions{nullptr},names.data(),inputs.data(),inputs.size(),&output,1);
        const float* values=result[0].GetTensorData<float>();std::vector<float> pooled(m_dimension,0.0f);
        for(qsizetype token=0;token<meaningful;++token)for(int d=0;d<m_dimension;++d)pooled[d]+=values[token*m_dimension+d];
        float norm=0;for(float& v:pooled){v/=float(meaningful);norm+=v*v;}norm=std::sqrt(qMax(norm,1e-12f));for(float& v:pooled)v/=norm;return pooled;
    }
private:
    void appendWordPiece(const QString& word,std::vector<qint64>& ids){
        if(m_vocab.contains(word)){ids.push_back(m_vocab[word]);return;}int start=0;QVector<int> parts;
        while(start<word.size()&&ids.size()+parts.size()<kMaxTokens-1){int end=word.size(),found=-1;while(end>start){const QString piece=(start?"##":"")+word.mid(start,end-start);if(m_vocab.contains(piece)){found=m_vocab[piece];break;}--end;}if(found<0){parts={m_vocab["[UNK]"]};break;}parts<<found;start=end;}for(int part:parts)ids.push_back(part);
    }
    QString m_model,m_hash;QHash<QString,int> m_vocab;Ort::Env m_env;Ort::Session m_session;std::vector<std::string> m_inputNames;std::string m_outputName;int m_dimension=0;
};

class Index {
public:
    Index(QString dbPath,QString directory,std::unique_ptr<Encoder> encoder):m_dbPath(std::move(dbPath)),m_dir(std::move(directory)),m_encoder(std::move(encoder)),m_space(m_encoder->dimension()) {QDir().mkpath(m_dir);openDb();load();}
    QJsonObject rebuild(quint64 requestId){
        QSqlQuery count(m_db);if(!count.exec("SELECT count(*) FROM passages p JOIN files f ON f.id=p.file_id WHERE f.removed_at IS NULL")||!count.next())return error(requestId,count.lastError().text());
        const size_t total=size_t(count.value(0).toULongLong());if(total==0)return error(requestId,"No indexed passages are available yet.");
        auto next=std::make_unique<hnswlib::HierarchicalNSW<float>>(&m_space,total,16,200);
        QSqlQuery q(m_db);if(!q.exec("SELECT p.id,p.text FROM passages p JOIN files f ON f.id=p.file_id WHERE f.removed_at IS NULL ORDER BY p.id"))return error(requestId,q.lastError().text());
        while(q.next()){const auto vector=m_encoder->encode("passage: "+q.value(1).toString());next->addPoint(vector.data(),size_t(q.value(0).toULongLong()));}
        const QString temp=m_dir+"/passages.hnsw.tmp",final=m_dir+"/passages.hnsw";next->saveIndex(temp.toStdString());QFile::remove(final);if(!QFile::rename(temp,final))return error(requestId,"Cannot publish semantic index.");
        QJsonObject metadata{{"schemaVersion",1},{"modelSha256",m_encoder->hash()},{"dimension",m_encoder->dimension()},{"chunkTokens",400},{"overlapTokens",60},{"passages",qint64(total)}};
        QSaveFile meta(m_dir+"/index.json");const QByteArray bytes=QJsonDocument(metadata).toJson();if(!meta.open(QIODevice::WriteOnly)||meta.write(bytes)!=bytes.size()||!meta.commit())return error(requestId,"Cannot publish semantic index metadata.");
        m_index=std::move(next);QSqlQuery version(m_db);version.prepare("INSERT OR REPLACE INTO embedding_versions(model_id,model_sha256,dimension,chunk_tokens,overlap_tokens,indexed_at) VALUES('intfloat/e5-small-v2',?,?,400,60,datetime('now'))");version.addBindValue(m_encoder->hash());version.addBindValue(m_encoder->dimension());version.exec();
        return QJsonObject{{"requestId",qint64(requestId)},{"message",QStringLiteral("Indexed %1 passages.").arg(total)},{"results",QJsonArray()}};
    }
    QJsonObject search(quint64 requestId,const QString& query,int limit){if(!m_index)return error(requestId,"Semantic index is unavailable. Build it from Settings.");return neighbors(requestId,m_encoder->encode("query: "+query),limit,-1);}
    QJsonObject related(quint64 requestId,int fileId,int ordinal,int limit){QSqlQuery q(m_db);q.prepare("SELECT text FROM passages WHERE file_id=? AND ordinal=?");q.addBindValue(fileId);q.addBindValue(ordinal);if(!q.exec()||!q.next())return error(requestId,"The selected passage is no longer indexed.");return neighbors(requestId,m_encoder->encode("query: "+q.value(0).toString()),limit,fileId);}
private:
    void openDb(){const QString name="semantic-db";m_db=QSqlDatabase::addDatabase("QSQLITE",name);m_db.setDatabaseName(m_dbPath);m_db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=10000");if(!m_db.open())throw std::runtime_error(m_db.lastError().text().toStdString());}
    void load(){QFile f(m_dir+"/index.json");if(!f.open(QIODevice::ReadOnly))return;const auto meta=QJsonDocument::fromJson(f.readAll()).object();if(meta["schemaVersion"].toInt()!=1||meta["modelSha256"].toString()!=m_encoder->hash()||meta["dimension"].toInt()!=m_encoder->dimension()||meta["chunkTokens"].toInt()!=400||meta["overlapTokens"].toInt()!=60)return;const QString path=m_dir+"/passages.hnsw";if(QFileInfo::exists(path))m_index=std::make_unique<hnswlib::HierarchicalNSW<float>>(&m_space,path.toStdString(),false,0,true);}
    QJsonObject neighbors(quint64 requestId,const std::vector<float>& vector,int limit,int excludedFile){
        const size_t wanted=qMin<size_t>(m_index->getCurrentElementCount(),size_t(qMax(limit*6,limit)));auto queue=m_index->searchKnn(vector.data(),wanted);struct Hit{float score;size_t passage;};QVector<Hit> hits;while(!queue.empty()){hits<<Hit{1.0f-queue.top().first,queue.top().second};queue.pop();}std::sort(hits.begin(),hits.end(),[](auto a,auto b){return a.score>b.score;});
        QSet<int> files;QJsonArray results;QSqlQuery passage(m_db);passage.prepare("SELECT p.file_id,p.ordinal,p.text,p.anchor_type,p.page_number,p.locator FROM passages p JOIN files f ON f.id=p.file_id WHERE p.id=? AND f.removed_at IS NULL");
        for(const Hit& hit:hits){passage.bindValue(0,qulonglong(hit.passage));if(!passage.exec()||!passage.next())continue;const int fileId=passage.value(0).toInt();if(fileId==excludedFile||files.contains(fileId))continue;files.insert(fileId);QJsonObject anchor{{"anchorType",passage.value(3).toString()},{"pageNumber",passage.value(4).toInt()},{"passageOrdinal",passage.value(1).toInt()},{"locator",passage.value(5).toString()},{"quote",passage.value(2).toString().left(500)}};results.append(QJsonObject{{"fileId",fileId},{"passageId",qint64(hit.passage)},{"passageOrdinal",passage.value(1).toInt()},{"text",passage.value(2).toString()},{"anchorType",passage.value(3).toString()},{"pageNumber",passage.value(4).toInt()},{"locator",passage.value(5).toString()},{"score",hit.score},{"anchor",anchor}});if(results.size()>=limit)break;}
        return QJsonObject{{"requestId",qint64(requestId)},{"results",results}};
    }
    QJsonObject error(quint64 id,const QString& message){return QJsonObject{{"requestId",qint64(id)},{"error",message.left(500)},{"results",QJsonArray()}};}
    QString m_dbPath,m_dir;std::unique_ptr<Encoder> m_encoder;hnswlib::InnerProductSpace m_space;std::unique_ptr<hnswlib::HierarchicalNSW<float>> m_index;QSqlDatabase m_db;
};
}

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);const QStringList args=app.arguments();const QString db=argument(args,"--database"),dir=argument(args,"--index-dir");
    QString model=argument(args,"--model"),vocab=argument(args,"--vocab");const QDir component(QFileInfo(app.applicationFilePath()).dir().filePath("../model"));if(model.isEmpty())model=component.filePath("model.onnx");if(vocab.isEmpty())vocab=component.filePath("vocab.txt");
    if(db.isEmpty()||dir.isEmpty()||!QFileInfo::exists(model)||!QFileInfo::exists(vocab)){respond({{"requestId",0},{"error","Semantic helper configuration is incomplete."}});return 2;}
    try {Index index(db,dir,std::make_unique<Encoder>(model,vocab));QTextStream in(stdin);while(!in.atEnd()){const auto request=QJsonDocument::fromJson(in.readLine().toUtf8()).object();const quint64 id=request["requestId"].toVariant().toULongLong();const QString command=request["command"].toString();if(command=="rebuild")respond(index.rebuild(id));else if(command=="search")respond(index.search(id,request["query"].toString(),qBound(1,request["limit"].toInt(40),100)));else if(command=="related")respond(index.related(id,request["fileId"].toInt(-1),request["passageOrdinal"].toInt(-1),qBound(1,request["limit"].toInt(20),100)));else respond({{"requestId",qint64(id)},{"error","Unknown semantic command."}});}}
    catch(const std::exception& e){respond({{"requestId",0},{"error",QString::fromUtf8(e.what()).left(500)}});return 1;}return 0;
}
