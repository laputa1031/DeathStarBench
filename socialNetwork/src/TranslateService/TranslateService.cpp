#include <signal.h>

#include <thrift/server/TThreadedServer.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

#include "../utils.h"
#include "TranslateHandler.h"

using apache::thrift::server::TThreadedServer;
using apache::thrift::transport::TServerSocket;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::protocol::TBinaryProtocolFactory;
using namespace social_network;

void sigintHandler (int sig) {
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sigintHandler);
    init_logger();
    SetUpTracer("config/jaeger-config.yml", "translate-service");

    json config_json;
    if (load_config_file("config/service-config.json", &config_json) == 0) {
        int port = config_json["translate-service"]["port"];
        std::string model_dir = config_json["translate_model_dir"];
        ctranslate2::TranslatorPool translator_pool(
            16, 1, model_dir, ctranslate2::Device::CUDA, {0,1}, ctranslate2::ComputeType::FLOAT16);
        ctranslate2::TranslationOptions options;
        options.max_batch_size = 50;
        options.beam_size = 1;
        options.return_scores = false;

        TThreadedServer server(
            std::make_shared<TranslateServiceProcessor>(
                std::make_shared<TranslateHandler>(
                    &translator_pool,
                    &options)),
            std::make_shared<TServerSocket>("0.0.0.0", port),
            std::make_shared<TFramedTransportFactory>(),
            std::make_shared<TBinaryProtocolFactory>()
        );

        std::cout << "Starting the translate-service server..." << std::endl;
        server.serve();
    } else exit(EXIT_FAILURE);
}
