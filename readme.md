First,build it:
```bash
mkdir build
cd build
cmake .. -DENABLE_CERVEUR_DEBUG=OFF
make
```
Second,run it:
```bash
./server 2> tsan_error.log
```
now you can visit this server via http://localhost:6969.

And last,test it (choose test parameters that you love):
```bash
curl --noproxy "*" http://localhost:6969
wrk -t4 -c100 -d20s http://127.0.0.1:6969/  
wrk -t6 -c500 -d20s --latency http://127.0.0.1:6969/
sudo /usr/lib/linux-tools/6.8.0-110-generic/perf recort -a -g -F 99 -- sleep 30  
sudo /usr/lib/linux-tools/6.8.0-110-generic/perf stat -p <PID> sleep 30  
sudo /usr/lib/linux-tools/6.8.0-110-generic/perf report
```