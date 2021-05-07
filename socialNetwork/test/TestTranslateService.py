import sys
sys.path.append('/home/wzhang/lyx/DeathStarBench/socialNetwork/gen-py')

from social_network import TranslateService
from thrift import Thrift
from thrift.transport import TSocket
from thrift.transport import TTransport
from thrift.protocol import TBinaryProtocol

try:
    transport = TSocket.TSocket('0.0.0.0', 9090)
    transport = TTransport.TBufferedTransport(transport)
    protocol = TBinaryProtocol.TBinaryProtocol(transport)
    client = TranslateService.Client(protocol)
    transport.open()
    msg = client.Translate(12343, "Hello!", {})
    print(msg)
    transport.close()

except Exception as e:
    print(e)
