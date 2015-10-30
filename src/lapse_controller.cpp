#include <prime_server/prime_server.hpp>
#include <prime_server/http_protocol.hpp>
#include <prime_server/zmq_helpers.hpp>
#include <prime_server/logging.hpp>

#include <unordered_map>
#include <vector>
#include <thread>
#include <random>
#include <functional>

using namespace prime_server;

void coordinate(zmq::context_t& context) {
  std::unordered_map<std::string, zmq::socket_t> services;
  zmq::beacon_t beacon;
  beacon.broadcast(zmq::random_port());
  beacon.subscribe();

  try {
    while(true) {
      //check for activity on the client socket and the result sockets
      std::vector<zmq::pollitem_t> items;
      items.reserve(services.size() + 1);
      for(auto& service : services)
        items.emplace_back(zmq::pollitem_t{service.second, 0, ZMQ_POLLIN, 0});
      items.emplace_back(zmq::pollitem_t{beacon, 0, ZMQ_POLLIN, 0});
      zmq::poll(items.data(), items.size(), 1000);

      //heard something from one of the services
      size_t i = 0;
      for(auto& service : services) {
        if(items[i++].revents & ZMQ_POLLIN) {
          auto messages = service.second.recv_all(0);
          for(const auto& message : messages) {
            logging::INFO(message.str());
          }
        }
      }

      //services are joining or dropping
      auto joined_dropped = beacon.update(items.back().revents & ZMQ_POLLIN);
      //join these
      for(const auto& service : joined_dropped.first) {
        logging::INFO(service.first + "(" + service.second + ") joined");
        zmq::socket_t socket(context, ZMQ_REQ);
        socket.connect(service.first.c_str());
        socket.send(std::string("{\"hello\":\"world\"}"), ZMQ_DONTWAIT);
        services.emplace(service.first, std::move(socket));
      }
      //drop these
      for(const auto& service : joined_dropped.second) {
        logging::INFO(service.first + "(" + service.second + ") dropped");
        services.erase(service.first);
      }
    }
  }
  catch(...) {
  }
}

struct front_end_t {
  worker_t::result_t work(const std::list<zmq::message_t>& job, void* request_info) {
    worker_t::result_t result{false};
    try {
      //echo
      http_response_t response(200, "OK", std::string(static_cast<const char*>(job.front().data()), job.front().size()));
      response.from_info(*static_cast<http_request_t::info_t*>(request_info));
      result.messages.emplace_back(response.to_string());
    }
    catch(const std::exception& e) {
      //complain
      worker_t::result_t result{false};
      http_response_t response(400, "Bad Request", e.what());
      response.from_info(*static_cast<http_request_t::info_t*>(request_info));
      result.messages.emplace_back(response.to_string());
    }
    return result;
  }
};

int main(int argc, char** argv) {
  if(argc < 2) {
    logging::ERROR("Usage: " + std::string(argv[0]) + "server_listen_endpoint concurrency");
    return 1;
  }

  //server endpoint
  std::string server_endpoint = argv[1];
  if(server_endpoint.find("://") == std::string::npos)
    logging::ERROR("Usage: " + std::string(argv[0]) + "server_listen_endpoint concurrency");

  //number of workers to use at each stage
  size_t worker_concurrency = 1;
  if(argc > 2)
    worker_concurrency = std::stoul(argv[2]);

  //change these to tcp://known.ip.address.with:port if you want to do this across machines
  zmq::context_t context;
  std::string result_endpoint = "ipc://result_endpoint";
  std::string proxy_endpoint = "ipc://proxy_endpoint";

  //a thread for coordinating with the cameras
  std::thread coordinator(std::bind(coordinate, context));


  //server
  std::thread server_thread = std::thread(std::bind(&http_server_t::serve,
    http_server_t(context, server_endpoint, proxy_endpoint + "_upstream", result_endpoint, true)));

  //load balancer for parsing
  std::thread front_end_proxy(std::bind(&proxy_t::forward, proxy_t(context, proxy_endpoint + "_upstream", proxy_endpoint + "_downstream")));
  front_end_proxy.detach();

  //front end threads
  std::list<std::thread> front_end_worker_threads;
  for(size_t i = 0; i < worker_concurrency; ++i) {
    front_end_t front_end;
    front_end_worker_threads.emplace_back(std::bind(&worker_t::work,
      worker_t(context, proxy_endpoint + "_downstream", "ipc://NO_ENDPOINT", result_endpoint,
      std::bind(&front_end_t::work, std::ref(front_end), std::placeholders::_1, std::placeholders::_2)
    )));
    front_end_worker_threads.back().detach();
  }

  server_thread.join();
  //TODO: should we listen for SIGINT and terminate gracefully/exit(0)?

  return 0;
}
