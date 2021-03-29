#ifndef SOCIAL_NETWORK_MICROSERVICES_TRANSLATEHANDLER_H
#define SOCIAL_NETWORK_MICROSERVICES_TRANSLATEHANDLER_H

#include <iostream>
#include <string>
#include <vector>
#include <chrono>

#include "../../gen-cpp/TranslateService.h"
#include "../../gen-cpp/ComposePostService.h"
#include "../logger.h"
#include "../tracing.h"
#include "../ClientPool.h"
#include "../ThriftClient.h"

#include <ctranslate2/translator.h>
#include <ctranslate2/translator_pool.h>


namespace social_network {

std::vector<std::vector<std::string>> tokenize(const std::vector<std::string>& inputs, std::string delimiter) {
    std::vector<std::vector<std::string>> ret;
    for (const auto& str: inputs) {
        size_t pos = 0, start = 0;
        std::vector<std::string> tokens;
        while ((pos = str.find(delimiter, start)) != std::string::npos) {
            tokens.emplace_back(str.substr(start, pos-start));
            start = pos + delimiter.size();
        }
        if (str.substr(start).size() > 0) {
            tokens.emplace_back(str.substr(start));
        }
        ret.emplace_back(tokens);
    }
    return ret;
}

class TranslateHandler : virtual public TranslateServiceIf {
 public:
  TranslateHandler(ctranslate2::TranslatorPool*, ctranslate2::TranslationOptions*);
  ~TranslateHandler() override = default;

  void Translate(std::string&, const int64_t, const std::string&, const std::map<std::string, std::string> &) override;
 private:
  ctranslate2::TranslatorPool* _translator_pool;
  ctranslate2::TranslationOptions* _options;
};

TranslateHandler::TranslateHandler(ctranslate2::TranslatorPool* translator_pool, ctranslate2::TranslationOptions* options) {
    _translator_pool = translator_pool;
    _options = options;
}

void TranslateHandler::Translate(
    std::string& _return,
    const int64_t req_id,
    const std::string &text,
    const std::map<std::string, std::string> &carrier) {

  TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  TextMapWriter writer(writer_text_map);
  auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  auto span = opentracing::Tracer::Global()->StartSpan(
      "Translate",
      { opentracing::ChildOf(parent_span->get()) });
  opentracing::Tracer::Global()->Inject(span->context(), writer);
  
  //begin to translate
  std::vector<std::string> inputs({text});
  auto batch = tokenize(inputs, " ");

  auto t1 = std::chrono::steady_clock::now();
  auto results = _translator_pool->translate_batch(batch, *_options);
  auto t2 = std::chrono::steady_clock::now();
  auto time_span = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
  std::cout << "req: " << req_id << "\ttranslation elapsed: " << time_span.count() << std::endl;
  _return.clear();
  for (const auto& result : results) {
      for (const auto& token : result.output()) {
          _return += token + " ";
      }
  }
  span->Finish();
}

} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_TRANSLATEHANDLER_H
