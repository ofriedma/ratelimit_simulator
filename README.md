# ratelimit_simulator
This project purpose is to test rate limiting algorithm easily and to decide about the most suitable algoorithm to count requests

please compile it like this:
g++ -pthread -ltbb -lboost_coroutine -lboost_system -std=c++2a simulator.cc -o simulator

./simulator <num reqs> <num_qos_tenants> <req_size> <backend bandwidth> <num retries> <wait between retries> <ops limit> <bytes limit>

This is the argument order

num reqs - the number of requests each tenant send
num_qos_tenants - number of tenants to test
req_size - each request size
backend_bandwidth - each request bandwidth (it is not overall but calculated for each request separately)
num retries -  how many retries until giving up on the request
wait between retries - how many ms to wait before retrying
ops limit - per ops/s for each tenant
bytes limit - per bytes/s for each tenant
