1. cmake .. -DENABLE_CERVEUR_DEBUG=OFF
2. ./server 2> tsan_error.log
3. curl --noproxy "*" http://localhost:6969
4. wrk -t4 -c100 -d20s http://127.0.0.1:6969/
5. wrk -t6 -c500 -d20s --latency http://127.0.0.1:6969/