#!/bin/bash
echo "klotski experiments"
cd ..
for file in $(ls /data/zyh/janus-v5/k_experiments); do
    echo ${file}
    
    #../bin/traffic_compressor /data/zyh/janus-v5/klotski_experiments/${file}/ /data/zyh/janus-v5/klotski_experiments/${file}/traffic
    #../bin/netre  /data/zyh/janus-v5/klotski_experiments/${file}/klotski-ssw.ini  -a long-term >/data/zyh/janus-v5/klotski_experiments/${file}/restmp.txt
    #../bin/netre  /data/zyh/janus-v5/klotski_experiments/${file}/klotski-fa.ini  -a long-term >/data/zyh/janus-v5/klotski_experiments/${file}/restmp.txt
    #../bin/netre  /data/zyh/janus-v5/klotski_experiments/${file}/klotski-ssw.ini -a pug-lookback >/data/zyh/janus-v5/klotski_experiments/${file}/res-fa.txt
    ./bin/netre  /data/zyh/janus-v5/k_experiments/${file}/klotski-fa.ini -a pug-lookback >/data/zyh/janus-v5/k_experiments/${file}/res-fa.txt
    ./bin/netre  /data/zyh/janus-v5/k_experiments/${file}/klotski-fa2.ini -a pug-lookback >/data/zyh/janus-v5/k_experiments/${file}/res-fa2.txt
done