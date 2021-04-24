#ifndef SOCIAL_NETWORK_MICROSERVICES_TRANSLATEHANDLER_H
#define SOCIAL_NETWORK_MICROSERVICES_TRANSLATEHANDLER_H

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <map>
#include <queue>
#include <algorithm>
#include <cstdlib>

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

std::vector<std::string> tokenize(const std::string& str, std::string delimiter) {
    std::vector<std::string> tokens;
    size_t pos = 0, start = 0;

    while ((pos = str.find(delimiter, start)) != std::string::npos) {
        tokens.emplace_back(str.substr(start, pos-start));
        start = pos + delimiter.size();
    }
    if (str.substr(start).size() > 0) {
        tokens.emplace_back(str.substr(start));
    }
    return tokens;
}

class TranslateHandler : virtual public TranslateServiceIf {
 public:
  TranslateHandler(ctranslate2::TranslatorPool*, ctranslate2::TranslationOptions*, int, int);
  ~TranslateHandler();

  void Translate(std::string&, const int64_t, const std::string&, const std::map<std::string, std::string> &) override;
 private:
  void work_loop();

  ctranslate2::TranslatorPool* _translator_pool;
  ctranslate2::TranslationOptions* _options;
  std::thread _processor;
  std::mutex _push_mutex;
  std::mutex _fetch_mutex;
  std::condition_variable _cv;
  std::condition_variable _can_push;
  std::condition_variable _can_fetch;
  std::map<int64_t, std::vector<std::string>> _buffer;
  std::queue<int64_t> _jobs;
  bool _req_end;
  int _fetch_available;

  int _max_wait_time;
  int _max_jobs;
};

TranslateHandler::TranslateHandler(ctranslate2::TranslatorPool* translator_pool, ctranslate2::TranslationOptions* options, int max_wait_time = 50, int max_jobs = 20) {
    _translator_pool = translator_pool;
    _options = options;
    _processor = std::thread(&TranslateHandler::work_loop, this);
    _req_end = false;
    _fetch_available = 0;
	_max_wait_time = max_wait_time;
	_max_jobs = max_jobs;
}

TranslateHandler::~TranslateHandler() {
    std::unique_lock<std::mutex> ul(_push_mutex);
    _req_end = true;
    ul.unlock();
    _can_push.notify_all();
    _processor.join();
}

void TranslateHandler::work_loop() {
    while (true) {
        std::unique_lock<std::mutex> push_ul(_push_mutex);
        _cv.wait_for(push_ul, std::chrono::milliseconds(_max_wait_time), [this]{return _jobs.size() >= _max_jobs or _req_end;});

        if (_req_end)
            break;

        if (_jobs.size() == 0)
            continue;
        int count = 0;
        std::vector<int64_t> reqs(std::min((int)_jobs.size(), _max_jobs));
        while (!_jobs.empty() and count < _max_jobs) {
            reqs[count] = _jobs.front();
            _jobs.pop();
            ++count;
        }
        push_ul.unlock();
        _can_push.notify_all();

        std::vector<std::vector<std::string>> batch(reqs.size());
        std::unique_lock<std::mutex> fetch_ul(_fetch_mutex);
        for (int i = 0; i < reqs.size(); ++i) {
            batch[i] = _buffer[reqs[i]];
        }
        fetch_ul.unlock();

        auto t1 = std::chrono::steady_clock::now();
        auto results = _translator_pool->translate_batch(batch, *_options);
        auto t2 = std::chrono::steady_clock::now();
        auto time_span = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
        std::cout << "batch size:" << batch.size() << "\ttranslation elapsed:" << time_span.count() << "ms" << std::endl;
        
        fetch_ul.lock();
        for (int i = 0; i < reqs.size(); ++i) {
            _buffer[reqs[i]] = results[i].output();
        }
        _fetch_available = reqs.size();
        fetch_ul.unlock();
        _can_fetch.notify_all();
    }
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

    auto t1 = std::chrono::steady_clock::now();
    auto tokens = tokenize(text, " ");
    auto t2 = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> push_ul(_push_mutex);
    if (_jobs.size() >= _max_jobs)
      _can_push.wait(push_ul, [this]{return _jobs.size() < 2 * _max_jobs;});

    _buffer[req_id] = tokens;
    _jobs.emplace(req_id);
    push_ul.unlock();
    _cv.notify_one();

    std::unique_lock<std::mutex> fetch_ul(_fetch_mutex);
    _can_fetch.wait(fetch_ul, [this]{return _fetch_available > 0;});
    auto translated = _buffer[req_id];
    _buffer.erase(req_id);
    --_fetch_available;
    fetch_ul.unlock();

    auto t3 = std::chrono::steady_clock::now();
    auto time_span_tokenize = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
    auto time_span_translate = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2);
    std::cout << "req:" << req_id << "\ttokenize elapsed:" << time_span_tokenize.count() << "ms\ttranslation elapsed:" << time_span_translate.count() << "ms" << std::endl;
    _return.clear();

    for (const auto& token : translated) {
      _return += token + " ";
    }
    span->Finish();
}

} //namespace social_network

#endif //SOCIAL_NETWORK_MICROSERVICES_TRANSLATEHANDLER_H
