bash 15-static-many.sh
bash 02-dynamic-experiment.sh
bash 09-failure-sweep.sh
bash 07-cost-cloud.sh
bash 10-step-count.sh 
bash 11-scale.sh
bash 13-bursty.sh
bash 14-bursty-more.sh
bash 12-scale-time.sh
bash 04-cost-time.sh

time ./bin/netre  /data/zyh/janus/experiments/time-8-12.ini -a pug-lookback >res01.txt
time ./bin/netre  /data/zyh/janus/experiments/time-16-24.ini -a pug-lookback >res02.txt
time ./bin/netre  /data/zyh/janus/experiments/time-24-32.ini -a pug-lookback >res03.txt
time ./bin/netre  /data/zyh/janus/experiments/time-32-48.ini -a pug-lookback >res04.txt

time ./bin/netre  /data/zyh/janus/experiments/time-32-48.ini -a pug-lookback >res04.txt
time ./bin/netre  /data/zyh/janus/experiments/time-8-12 copy.ini -a pug-lookback >res01.txt
time ./bin/netre  /data/zyh/janus/experiments/time-16-24 copy.ini -a pug-lookback >res02.txt
time ./bin/netre  /data/zyh/janus/experiments/time-24-32 copy.ini -a pug-lookback >res03.txt
time ./bin/netre  /data/zyh/janus/experiments/time-32-48 copy.ini -a pug-lookback >res04.txt
time ./bin/netre  /data/zyh/janus/experiments/time-8-12.ini -a pug-lookback >res01.txt