#include "rgw_qos_tbb.h"
#include "random"
#include <cstdlib>
#include <string>
#include <future>
#include <boost/asio.hpp>
std::uniform_int_distribution<unsigned int> dist(0, 1);
std::random_device urandom("/dev/urandom");
std::uniform_int_distribution<unsigned long long> disttenant(2, 100000000000);

std::string method[2] = {"PUT", "GET"};
void load_datastructure(std::shared_ptr<QosDatastruct> qos, std::atomic_uint64_t *timediff)
{
    using namespace std::chrono_literals;
    unsigned long long tenantid = disttenant(urandom);
    int rw = dist(urandom);
    std::string tenant = "uuser"; //+ std::to_string(tenantid);
    RGWQoSInfo info;
    info.enabled = true;
    info.max_read_bytes = 100 * 1024 * 1024;
    info.max_write_bytes = info.max_read_bytes;
    info.max_read_ops = 200;
    info.max_write_ops = 200;
    std::string methodop = method[rw];
    auto time = std::chrono::system_clock::now();
    qos->increase_entry(methodop.c_str(), tenant, time, info);
    qos->increase_bw(methodop.c_str(),tenant, 1024, info);
    qos->increase_bw(methodop.c_str(),tenant, 1024, info);
    qos->increase_bw(methodop.c_str(),tenant, 1024, info);
    qos->increase_bw(methodop.c_str(),tenant, 1024, info);

    qos->decrease_concurrent_ops(methodop.c_str(),tenant);
    auto end = std::chrono::system_clock::now();
    *timediff += std::chrono::duration_cast<std::chrono::microseconds>(end - time).count();
    //std::cerr << std::chrono::duration_cast<std::chrono::microseconds>(end - time).count() << std::endl;


}
int main()
{
    std::atomic_uint64_t timediff = 0;
    boost::asio::thread_pool pool(40);

    using namespace std::chrono_literals;
    std::shared_ptr<QosActiveDatastruct> qos(new QosActiveDatastruct());
    qos->start();
    for(int i = 0; i < 2000000; i++) {
        boost::asio::post(pool, std::bind(load_datastructure,qos->get_active(), &timediff));
    }
    pool.join();
    std::cerr << timediff / 1000000.0 << std::endl;
    
}
