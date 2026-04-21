1. make
2. ./server
3. curl --noproxy "*" http://localhost:6969
4. wrk -t4 -c100 -d20s http://127.0.0.1:6969/
5. wrk2 -t1 -c100 -d20s -R10000 http://127.0.0.1:6969/