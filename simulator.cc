#include "rgw_qos_tbb.h"
#include "random"
#include <cstdlib>
#include <string>
#include <future>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <atomic>
std::uniform_int_distribution<unsigned int> dist(0, 1);
std::random_device urandom("/dev/urandom");
std::uniform_int_distribution<unsigned long long> disttenant(2, 100000000);
std::mutex lock;
struct tenant_info {
    std::atomic_uint64_t accepted = 0;
    std::atomic_uint64_t rejected = 0;
    std::atomic_uint64_t ops = 0;
    std::atomic_uint64_t bytes = 0;
    std::atomic_uint64_t num_retries = 0;
};
std::unordered_map<std::string, tenant_info> ds;

std::string method[2] = {"PUT", "GET"};
// This function is simulating a single client request
bool load_datastructure(std::shared_ptr<QosDatastruct> qos, std::string tenant, int req_size, int backend_bandwidth,  int ops_limit, int bw_limit, 
                        boost::asio::steady_timer& timer, boost::asio::yield_context ctx)
{
    using namespace std::chrono_literals;
    unsigned long long tenantid = disttenant(urandom);
    int rw = dist(urandom);
    rw = 0;
    RGWQoSInfo info;
    info.enabled = true;
    info.max_read_bytes = bw_limit;
    info.max_write_bytes = bw_limit;
    info.max_read_ops = ops_limit;
    info.max_write_ops = ops_limit;
    std::string methodop = method[rw];
    auto time = std::chrono::system_clock::now();
    auto x = qos->increase_entry(methodop.c_str(), tenant, time, info);
    lock.lock();
    auto stats = ds.emplace(std::piecewise_construct,
               std::forward_as_tuple(tenant),
               std::forward_as_tuple()).first;
    lock.unlock();
    if(x)
    {
        stats->second.rejected++;
        stats->second.ops++;
        qos->decrease_concurrent_ops(methodop.c_str(),tenant);
        return true;
    }
    stats->second.accepted++;
    stats->second.bytes += req_size;
    stats->second.ops++;
// the 4 * 1024 * 1024 is the RGW default we are sending in a typical environment
    while (req_size) {
        if(req_size <= backend_bandwidth) {
            for(;req_size > 0;) {
                if(req_size > 4*1024*1024) {
                    qos->increase_bw(methodop.c_str(),tenant, 4*1024*1024, info);
                    req_size = req_size - 4*1024*1024;
                }
                else {
                    qos->increase_bw(methodop.c_str(),tenant, req_size, info);
                    req_size = 0;
                }
            }
        } else {
                uint64_t total_bytes = 0;
                for(;req_size > 0;) {
                if(req_size >= 4*1024*1024) {
                    if(total_bytes >= backend_bandwidth)
                    {
                        timer.expires_after(std::chrono::seconds(1));
                        timer.async_wait(ctx);
                        total_bytes = 0;
                    }
                    qos->increase_bw(methodop.c_str(),tenant, 4*1024*1024, info);
                    req_size = req_size - 4*1024*1024;
                    total_bytes += 4*1024*1024;
                }
                else {
                    qos->increase_bw(methodop.c_str(),tenant, req_size, info);
                    total_bytes += req_size;
                    req_size = 0;
                }
            }
        }
    }
    qos->decrease_concurrent_ops(methodop.c_str(),tenant);
    auto end = std::chrono::system_clock::now();
    return false;
}

int main(int argc, char **argv)
{
    if (argc != 9) {
        std::cerr << argv[0] << " <num reqs> <num_qos_tenants> <req_size> <backend bandwidth> <num retries> <wait between retries>" 
        << " <ops limit> <bytes limit>" << std::endl;
        return 1;
    }
    int num_reqs = strtol(argv[1], nullptr, 10); // how many requests per qos tenant
    int num_qos_classes = strtol(argv[2], nullptr, 10); // how many qos tenants
    int request_size = strtol(argv[3], nullptr, 10); // what is the request size we are testing if 0, it will be randomized
    int backend_bandwidth = strtol(argv[4], nullptr, 10); // what is the backend bandwidth, so there will be wait between increase_bw
    int num_of_retires = strtol(argv[5], nullptr, 10); // how many retries before fail
    int wait_between_retries_ms = strtol(argv[6], nullptr, 10); // time in seconds to wait between retries
    int ops_limit = strtol(argv[7], nullptr, 10); // ops limit for the tenants
    int bw_limit = strtol(argv[8], nullptr, 10); // bytes per second limit
    std::cerr << bw_limit << std::endl;
    std::shared_ptr<QosActiveDatastruct> qos(new QosActiveDatastruct());
    qos->start();
    std::vector<std::thread> threads;
    using Executor = boost::asio::io_context::executor_type;
    std::optional<boost::asio::executor_work_guard<Executor>> work;
    const int thread_count = 512;
    threads.reserve(thread_count);
    boost::asio::io_context context;
    work.emplace(boost::asio::make_work_guard(context));
    // server execution
    for (int i = 0; i < thread_count; i++) {
      threads.emplace_back([&]() noexcept {
        context.run();
      });
    }
    //client execution
    for(int i = 0; i < num_qos_classes; i++)
    {
        
        unsigned long long tenantid = disttenant(urandom);
        std::string tenantuser = "uuser" + std::to_string(tenantid);
        std::string tenantbucket = "bbucket" + std::to_string(tenantid);
        for(int j = 0; j < num_reqs; j++)
            boost::asio::spawn(context,
                [&, tenantuser, tenantbucket](boost::asio::yield_context ctx) {
                    boost::asio::steady_timer timer(context);
                    // there are bucket and user tenants just as it will appear in the real scenario
                    bool user = load_datastructure(qos->get_active(), tenantuser, request_size, backend_bandwidth, ops_limit, bw_limit, timer, ctx);
                    bool bucket = load_datastructure(qos->get_active(), tenantbucket, request_size, backend_bandwidth, ops_limit, bw_limit, timer, ctx);
                    int tries_counter = 0;
                    // retries mechanism
                    while((user || bucket) && (tries_counter <= num_of_retires))
                    {
                        auto it = ds.find(tenantuser);
                        it->second.num_retries++;
                        it = ds.find(tenantbucket);
                        it->second.num_retries++;
                        tries_counter++;
                        timer.expires_after(std::chrono::milliseconds(wait_between_retries_ms));
                        timer.async_wait(ctx);
                        bool user = load_datastructure(qos->get_active(), tenantuser, request_size, backend_bandwidth, ops_limit, bw_limit, timer, ctx);
                        bool bucket = load_datastructure(qos->get_active(), tenantbucket, request_size, backend_bandwidth, ops_limit, bw_limit, timer, ctx);
                    }
                });
    }
    work.reset();
    for(auto& i : threads)
        i.join();
    for(auto& i : ds) {
        std::cerr << "Tenant is: " << i.first << " accepted " << i.second.accepted << " rejected: " 
                  << i.second.rejected << " ops: " << i.second.ops << " bytes: " << i.second.bytes 
                  << " num retries: " << i.second.num_retries << std::endl;
    }
    std::cerr << "Simulator finished" << std::endl;
    return 0;

}