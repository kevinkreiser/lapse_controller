#include <prime_server/prime_server.hpp>
#include <prime_server/http_protocol.hpp>
#include <prime_server/zmq_helpers.hpp>
#include <prime_server/logging.hpp>

#include <unordered_map>
#include <vector>
#include <thread>
#include <random>
#include <functional>
#include <regex>
#include <ctime>

using namespace prime_server;

struct camera_t {
  zmq::socket_t socket;
  std::string uuid;
  std::string info;
  std::string next;
  std::time_t wait_until;

  camera_t(zmq::context_t& context, const std::string& endpoint, const std::string& uuid):
    socket(context, ZMQ_REQ), uuid(uuid), info(), next(), wait_until(0) {
    socket.connect(endpoint.c_str());
  }

  bool handle_response() {
    auto response = socket.recv_all(0).front().str();
    switch(response.front()) {
      case 'I': //INFO for the camera
        info = response.substr(1);
        socket.send(std::string("N"), ZMQ_DONTWAIT);
        return true;
      case 'N': //NEXT camera image name
        next = response.substr(1);
        if(next.size())
          socket.send(std::string("C" + next), ZMQ_DONTWAIT);
        break;
      case 'W': //WAIT so many seconds before asking again
        wait_until = std::time(nullptr) + std::stoul(response.substr(1));
        break;
      case 'C': //CAMERA image data is here
        //TODO: actually save the file
        next.clear();
        socket.send(std::string("N"), ZMQ_DONTWAIT);
        break;
      default:
        logging::WARN("Unrecognized response: " + response);
        break;
    }
    return false;
  }

  void nag() {
    if(wait_until && wait_until < std::time(nullptr)) {
      socket.send(std::string("N"), ZMQ_DONTWAIT);
      wait_until = 0;
    }
  }
};

void coordinate(zmq::context_t& context) {
  std::unordered_map<std::string, camera_t> cameras;
  zmq::beacon_t beacon;
  beacon.broadcast(zmq::random_port());
  beacon.subscribe();

  try {
    while(true) {
      //check for activity on the client socket and the result sockets
      std::vector<zmq::pollitem_t> items;
      items.reserve(cameras.size() + 1);
      for(auto& camera : cameras)
        items.emplace_back(zmq::pollitem_t{camera.second.socket, 0, ZMQ_POLLIN, 0});
      items.emplace_back(zmq::pollitem_t{beacon, 0, ZMQ_POLLIN, 0});
      zmq::poll(items.data(), items.size(), 1000);

      //heard something from one of the cameras
      size_t i = 0;
      bool update_status = false;
      for(auto& camera : cameras) {
        //heard something from one of the cameras
        if(items[i++].revents & ZMQ_POLLIN)
          update_status = camera.second.handle_response() || update_status;
        else
          camera.second.nag();
      }

      //services are joining or dropping
      auto joined_dropped = beacon.update(items.back().revents & ZMQ_POLLIN);
      if(joined_dropped.first.size() || joined_dropped.second.size()) {
        update_status = true;
        //join these
        for(const auto& service : joined_dropped.first) {
          logging::INFO(service.first + "(" + service.second + ") joined");
          camera_t camera(context, service.first, service.second);
          camera.socket.send(std::string("I"), ZMQ_DONTWAIT); //INFO
          cameras.emplace(service.first, std::move(camera));
        }
        //drop these
        for(const auto& service : joined_dropped.second) {
          logging::INFO(service.first + "(" + service.second + ") dropped");
          cameras.erase(service.first);
        }
      }

      //TODO: drop updated status json
      if(update_status) {

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
      //only let very specific requests through
      auto request = http_request_t::from_string(static_cast<const char*>(job.front().data()), job.front().size());
      if (request.path == "/status.json" || std::regex_match(request.path, std::regex("/[a-f0-9]+/[0-9_]+\\.JPG"))) {
        std::fstream input(request.path, std::ios::in | std::ios::binary);
        if(input) {
          std::string buffer((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
          http_response_t response(200, "OK", buffer);
          response.from_info(*static_cast<http_request_t::info_t*>(request_info));
          result.messages.emplace_back(response.to_string());
        }
      }
      //didn't make the cut
      http_response_t response(404, "Not Found", "try /status.json");
      response.from_info(*static_cast<http_request_t::info_t*>(request_info));
      result.messages.emplace_back(response.to_string());
    }
    catch(const std::exception& e) {
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
