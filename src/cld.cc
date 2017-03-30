#include "compact_lang_det.h"
#include "encodings.h"
#include "constants.h"
#include "nan.h"

using std::terminate_handler;
using std::unexpected_handler;
using Nan::AsyncQueueWorker;
using Nan::AsyncWorker;
using Nan::Callback;
using Nan::HandleScope;
using Nan::New;
using Nan::Null;
using Nan::To;

namespace NodeCld {
class DetectWorker : public AsyncWorker
{
  public:
    DetectWorker(Callback *callback,
                 v8::Local<v8::String> text,
                 int buffer_length,
                 bool is_plain_text,
                 CLD2::CLDHints tld_hint,
                 int flag)
        : AsyncWorker(callback),
          buffer(text),
          buffer_length(buffer_length),
          is_plain_text(is_plain_text),
          tld_hint(tld_hint),
          flag(flag)
    {
    }
    ~DetectWorker() {}

    // Executed inside the worker-thread.
    // It is not safe to access V8, or V8 data structures
    // here, so everything we need for input and output
    // should go on `this`.
    void Execute()
    {
      CLD2::ExtDetectLanguageSummary(
          *buffer,
          buffer_length,
          is_plain_text,
          &tld_hint,
          flag,
          language3,
          percent3,
          normalized_score3,
          &resultChunkVector,
          &textBytesFound,
          &isReliable);
    }

    // Executed when the async work is complete
    // this function will be run inside the main event loop
    // so it is safe to use V8 again
    void HandleOKCallback()
    {
      HandleScope scope;

      v8::Local<v8::Object> results = Nan::New<v8::Object>();
      unsigned int languageIdx = 0;
      v8::Local<v8::Array> languages = v8::Local<v8::Array>(Nan::New<v8::Array>());
      for (int resultIdx = 0; resultIdx < 3; resultIdx++)
      {
        CLD2::Language lang = language3[resultIdx];

        if (lang == CLD2::UNKNOWN_LANGUAGE)
        {
          continue;
        }

        v8::Local<v8::Object> item = Nan::New<v8::Object>();
        Nan::Set(item, Nan::New<v8::String>("name").ToLocalChecked(),
                 Nan::New<v8::String>(Constants::getInstance().getLanguageName(lang)).ToLocalChecked());
        Nan::Set(item, Nan::New<v8::String>("code").ToLocalChecked(),
                 Nan::New<v8::String>(Constants::getInstance().getLanguageCode(lang)).ToLocalChecked());
        Nan::Set(item, Nan::New<v8::String>("percent").ToLocalChecked(),
                 Nan::New<v8::Number>(percent3[resultIdx]));
        Nan::Set(item, Nan::New<v8::String>("score").ToLocalChecked(),
                 Nan::New<v8::Number>(normalized_score3[resultIdx]));

        Nan::Set(languages, Nan::New<v8::Integer>(languageIdx), item);
        languageIdx++;
      }

      if (languages->Length() < 1) {
        v8::Local<v8::Object> item = Nan::New<v8::Object>();
        Nan::Set(item, Nan::New<v8::String>("message").ToLocalChecked(),
                 Nan::New<v8::String>("Failed to identify language").ToLocalChecked());
        v8::Local<v8::Value> argv[] = {
            item, Null()
        };

        callback->Call(2, argv);
        return;
      }

      unsigned int chunkIdx = 0;
      v8::Local<v8::Array> chunks = v8::Local<v8::Array>(Nan::New<v8::Array>());
      for (unsigned int resultIdx = 0; resultIdx < resultChunkVector.size(); resultIdx++)
      {
        CLD2::ResultChunk chunk = resultChunkVector.at(resultIdx);
        CLD2::Language lang = static_cast<CLD2::Language>(chunk.lang1);

        if (lang == CLD2::UNKNOWN_LANGUAGE)
        {
          continue;
        }

        v8::Local<v8::Object> item = Nan::New<v8::Object>();
        Nan::Set(item, Nan::New<v8::String>("name").ToLocalChecked(),
                 Nan::New<v8::String>(Constants::getInstance().getLanguageName(lang)).ToLocalChecked());
        Nan::Set(item, Nan::New<v8::String>("code").ToLocalChecked(),
                 Nan::New<v8::String>(Constants::getInstance().getLanguageCode(lang)).ToLocalChecked());
        Nan::Set(item, Nan::New<v8::String>("offset").ToLocalChecked(),
                 Nan::New<v8::Number>(chunk.offset));
        Nan::Set(item, Nan::New<v8::String>("bytes").ToLocalChecked(),
                 Nan::New<v8::Number>(chunk.bytes));

        Nan::Set(chunks, Nan::New<v8::Integer>(chunkIdx), item);
        chunkIdx++;
      }

      Nan::Set(results, Nan::New<v8::String>("reliable").ToLocalChecked(),
               Nan::New<v8::Boolean>(isReliable));
      Nan::Set(results, Nan::New<v8::String>("textBytes").ToLocalChecked(),
               Nan::New<v8::Number>(textBytesFound));
      Nan::Set(results, Nan::New<v8::String>("languages").ToLocalChecked(),
               languages);
      Nan::Set(results, Nan::New<v8::String>("chunks").ToLocalChecked(),
               chunks);

      v8::Local<v8::Value> argv[] = {
          Null(), results};

      callback->Call(2, argv);
    }

  private:
    v8::String::Utf8Value buffer;
    int buffer_length;
    bool is_plain_text;
    CLD2::CLDHints tld_hint;
    int flag;
    CLD2::Language language3[3];
    int percent3[3];
    double normalized_score3[3];
    CLD2::ResultChunkVector resultChunkVector;
    int textBytesFound;
    bool isReliable;
  };

  NAN_METHOD(Detect) {
    v8::String::Utf8Value text(info[0]->ToString());

    int numBytes     = text.length();
    bool isPlainText = info[1]->ToBoolean()->Value();

    CLD2::CLDHints hints;
    hints.tld_hint = 0;
    hints.content_language_hint = 0;
    hints.language_hint = CLD2::UNKNOWN_LANGUAGE;
    hints.encoding_hint = CLD2::UNKNOWN_ENCODING;

    v8::String::Utf8Value languageHint(info[2]->ToString());
    v8::String::Utf8Value encodingHint(info[3]->ToString());
    v8::String::Utf8Value tldHint(info[4]->ToString());
    v8::String::Utf8Value httpHint(info[5]->ToString());

    if (tldHint.length() > 0) {
      hints.tld_hint = *tldHint;
    }
    if (httpHint.length() > 0) {
      hints.content_language_hint = *httpHint;
    }
    if (languageHint.length() > 0) {
      hints.language_hint = Constants::getInstance().getLanguageFromName(*languageHint);
    }
    if (encodingHint.length() > 0) {
      hints.encoding_hint = Constants::getInstance().getEncodingFromName(*encodingHint);
    }

    Callback *callback = new Callback(info[6].As<v8::Function>());
    AsyncQueueWorker(new DetectWorker(
      callback, 
      info[0]->ToString(),
      numBytes,
      isPlainText,
      hints,
      0
    ));
  }

  extern "C" void init (v8::Handle<v8::Object> target) {
    // set detected languages
    v8::Local<v8::Array> detected = Nan::New<v8::Array>();
    vector<NodeCldDetected>* rawDetected = Constants::getInstance().getDetected();
    for(vector<NodeCldDetected>::size_type i = 0; i < rawDetected->size(); i++) {
      NodeCldDetected rawLanguage = rawDetected->at(i);
      Nan::Set(detected, static_cast<uint32_t>(i),
          Nan::New<v8::String>(rawLanguage.name).ToLocalChecked());
    }
    Nan::Set(target, Nan::New<v8::String>("DETECTED_LANGUAGES").ToLocalChecked(), detected);

    // set all languages
    v8::Local<v8::Object> languages = Nan::New<v8::Object>();
    vector<NodeCldLanguage>* rawLanguages = Constants::getInstance().getLanguages();
    for(vector<NodeCldLanguage>::size_type i = 0; i < rawLanguages->size(); i++) {
      NodeCldLanguage rawLanguage = rawLanguages->at(i);
      Nan::Set(languages, Nan::New<v8::String>(rawLanguage.name).ToLocalChecked(),
          Nan::New<v8::String>(rawLanguage.code).ToLocalChecked());
    }
    Nan::Set(target, Nan::New<v8::String>("LANGUAGES").ToLocalChecked(), languages);

    // set encodings
    v8::Local<v8::Array> encodings = Nan::New<v8::Array>();
    vector<NodeCldEncoding>* rawEncodings = Constants::getInstance().getEncodings();
    for(vector<NodeCldEncoding>::size_type i = 0; i < rawEncodings->size(); i++) {
      NodeCldEncoding rawEncoding = rawEncodings->at(i);
      Nan::Set(encodings, static_cast<uint32_t>(i),
          Nan::New<v8::String>(rawEncoding.name).ToLocalChecked());
    }
    Nan::Set(target, Nan::New<v8::String>("ENCODINGS").ToLocalChecked(), encodings);

    Nan::SetMethod(target, "detect", Detect);
  }

  NODE_MODULE(cld, init);
}
