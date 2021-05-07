import os
import re
import time
from math import ceil
import subprocess as sp
import psutil
import numpy as np


RPS = 100
WRK_EXECUTABLE = "/home/wzhang/wrk2/wrk"
WRK_SCRIPT = "/home/wzhang/lyx/DeathStarBench/socialNetwork/wrk2/scripts/social-network/compose-post-translate.lua"
WRK_ARGS = " --timeout 20s -c {} -d {} -t {} -s {} http://localhost:8080/wrk2-api/post/compose -R {} -L"
WRK_COMMAND = WRK_EXECUTABLE + ' ' + WRK_ARGS.format(128, 30, 2, WRK_SCRIPT, RPS)


def modify(percentage, scale, batchsize, waittime):
    newContent = ''
    with open('docker-compose-translate-mps.yml', 'r') as f:
        for line in f:
            line = re.sub('CUDA_MPS_ACTIVE_THREAD_PERCENTAGE=[0-9.]+','CUDA_MPS_ACTIVE_THREAD_PERCENTAGE={}'.format(percentage), line)
            line = re.sub('scale: [0-9]+', 'scale: {}'.format(scale), line)
            line = re.sub('"python3", "src/TranslateService/TranslateService.py", "[0-9]+", "[0-9.]+"',
                          '"python3", "src/TranslateService/TranslateService.py", "{}", "{}"'.format(batchsize, waittime),
                          line)
            newContent += line
    with open('docker-compose-translate-mps.yml', 'w') as f:
        f.write(newContent)


def execAndGetResult(cmd):
    res = os.popen(cmd)
    output = res.read()
    res.close()
    return output


def parseWrkResult(text):
    latency = []
    for s in ["90.000%", "99.000%", "99.900%"]:
        res = re.search(s+" *([0-9.]*)", text)
        if res:
            latency.append(res.group(1))
    print(latency)
    res = re.search("Requests/sec: *([0-9.]+)", text)
    throughput = 0.0
    if res:
        throughput = res.group(1)
    print("throughput:", throughput)
    errorResp = 0
    res = re.search("Non-2xx or 3xx responses: *([0-9]+)", text)
    if res:
        errorResp = res.group(1)
    return latency, throughput, errorResp


def restartService():
    print("########restarting social network service########")
    cmd_up = "docker-compose -f docker-compose-translate-mps.yml up -d"
    cmd_down = "docker-compose -f docker-compose-translate-mps.yml down"
    os.system(cmd_down)
    os.system(cmd_up)
    while float(psutil.cpu_percent()) >5.0:  # wait for model init
        time.sleep(10)
    print("########restarting social network service done########")


def test(percentage, scale, batchsize, waittime, outfile):
    modify(percentage, scale, batchsize, waittime)
    restartService()
    wrk_output = execAndGetResult(WRK_COMMAND)
    latency, throughput, errorResp = parseWrkResult(wrk_output)
    latencies = ', '.join(latency)
    data = str(percentage) + ', ' + str(scale) + ', ' + str(batchsize) + ', ' + str(waittime) + ', ' +  latencies+ ', ' + str(throughput) + ', ' + str(errorResp) + '\n'
    print(data)
    with open(outfile, 'a') as f:
        f.write(data)


if __name__ == '__main__':
    outfile = 'test_output_rps_{}.txt'.format(RPS)
    with open(outfile, 'a') as f:
        f.write("percentage(per model), scale, batchsize, waittime, 90.0%, 99.0%, 99.9%, requests/sec, error responses\n")
    total_percentage = 100
    for scale in range(1, 5):  # #4
        percentage = ceil(total_percentage / scale)
        for batchsize in range(10, RPS+1, 10):  # 10
            for waittime in np.arange (0.1, 0.51, 0.05): #20
                waittime = '%.2f' % waittime
                test(percentage, scale, batchsize, waittime, outfile)

