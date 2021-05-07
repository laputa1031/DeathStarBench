import sys
sys.path.append('/social-network-microservices/gen-py')

from social_network import TranslateService
from social_network.ttypes import *

from thrift.transport import TSocket
from thrift.transport import TTransport
from thrift.protocol import TBinaryProtocol
from thrift.server import TServer
from jaeger_client import Config
from opentracing.propagation import Format

import ctranslate2
import pyonmttok
from service_streamer import ThreadedStreamer, ManagedModel
import time
import json
import yaml
import logging
logging.basicConfig(stream=sys.stdout)
logger = logging.getLogger(__name__)
logger.setLevel("DEBUG")


class ManagedCTranslateModel(ManagedModel):
    def init_model(self, *args, **kwargs):
        model_dir = kwargs.get("model_dir", "/social-network-microservices/data/ende_ctranslate2")
        device = kwargs.get("device", "cuda")
        self.model = ctranslate2.Translator(model_dir, device=device)
        self.model.translate_batch([["a"]])  # pre warm

    def predict(self, batch):
        #logger.info("model predict!")
        return self.model.translate_batch(batch, max_batch_size=50, beam_size=1, return_scores=False)


class TransFunc:
    def __init__(self):
        self.model =  ctranslate2.Translator("/social-network-microservices/data/ende_ctranslate2", device="cuda")
        #self.model.translate_batch([["a"]])  # pre warm

    def predict(self, batch):
        #logger.info("model predict!")
        #return self.model.translate_batch(batch, max_batch_size=50, beam_size=1, return_scores=False)
        return batch

            
class TranslateHandler:
    def __init__(self, tokenizer, streamer, tracer):
        self.tokenizer = pyonmttok.Tokenizer('conservative', joiner_annotate=True) if tokenizer is None else tokenizer
        assert streamer is not None
        assert tracer is not None
        self.streamer = streamer
        self.tracer = tracer

    def Translate(self, req_id, text, carrier):
        span_ctx = self.tracer.extract(format=Format.TEXT_MAP, carrier=carrier)
        with self.tracer.start_span("translate-service", child_of=span_ctx) as span:
            tokens, _ = self.tokenizer.tokenize(text)
            t1 = time.time()
            outputs = self.streamer.predict([tokens])
            t2 = time.time()
            result = "this is a test !"
            #result = self.tokenizer.detokenize(outputs[0][0]['tokens'])
            #logger.info("translate time elapsed:{:.4f}ms".format((t2-t1)*1000))
        return result


if __name__ == '__main__':
    
    batch_size = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    assert batch_size > 0, "invalid batch size:{}".format(batch_size)
    wait_time = float(sys.argv[2]) if len(sys.argv) > 2 else 0.001
    assert wait_time > 0.0, "invalid wait time:{}".format(wait_time)

    try:
        with open("config/service-config.json", 'r') as f:
            config_json = json.load(f)
    except Exception as e:
        logger.error(e)
        exit()
    try:
        with open("config/jaeger-config.json", 'r') as f:
            jaeger_config_json = json.load(f)
    except Exception as e:
        logger.error(e)
        exit()

    port = int(config_json["translate-service"]["port"])
    model_dir = config_json["translate_model_dir"]
    logger.info("port:{}".format(port))
    logger.info("model:{}".format(model_dir))
    #model_dir = "/home/wzhang/lyx/DeathStarBench/socialNetwork/data/ende_ctranslate2"
    translate_func = TransFunc()
    jaeger_config = Config(config=jaeger_config_json, service_name="translate-service", validate=True)
    tracer = jaeger_config.initialize_tracer()
    tokenizer = pyonmttok.Tokenizer('conservative', joiner_annotate=True)
    streamer = ThreadedStreamer(translate_func.predict, batch_size=batch_size, max_latency=wait_time) 
    handler = TranslateHandler(tokenizer = tokenizer, streamer=streamer, tracer=tracer)
    processor = TranslateService.Processor(handler)
    transport = TSocket.TServerSocket("0.0.0.0", port)
    tfactory = TTransport.TFramedTransportFactory()
    pfactory = TBinaryProtocol.TBinaryProtocolFactory()

    server = TServer.TThreadedServer(
        processor, transport, tfactory, pfactory)
    
    logger.info("Starting the translate-service server...")
    server.serve()
    logger.info("translate-service server exit...")

