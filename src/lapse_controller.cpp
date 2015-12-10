#include <prime_server/prime_server.hpp>
#include <prime_server/http_protocol.hpp>
#include <prime_server/http_util.hpp>
#include <prime_server/zmq_helpers.hpp>
#include <prime_server/logging.hpp>

#include <unordered_map>
#include <vector>
#include <thread>
#include <random>
#include <functional>
#include <regex>
#include <atomic>
#include <ctime>
#include <csignal>
#include <cstdlib>

using namespace prime_server;

volatile std::atomic<bool> running;
const headers_t::value_type CORS{"Access-Control-Allow-Origin", "*"};
const headers_t::value_type HTML_MIME{"Content-type", "text/html;charset=utf-8"};
const headers_t::value_type JS_MIME{"Content-type", "application/javascript;charset=utf-8"};

struct camera_t {
  zmq::socket_t socket;
  std::string uuid;
  std::string settings;
  std::string next;
  std::time_t wait_until;

  camera_t(zmq::context_t& context, const std::string& endpoint, const std::string& uuid):
    socket(context, ZMQ_REQ), uuid(uuid), settings(), next(), wait_until(0) {
    while(this->uuid.size() && this->uuid.back() == '\0')
      this->uuid.pop_back();
    socket.connect(endpoint.c_str());
  }

  bool handle_response(const std::string& www_dir) {
    auto response = socket.recv_all(0).front().str();
    logging::DEBUG(uuid + ":" + response.substr(0, 1));
    switch(response.front()) {
      case 'I': //INFO for the camera
        socket.send(std::string("N"), ZMQ_DONTWAIT);
        settings = response.substr(1);
        return true;
      case 'N': //NEXT camera image name
        next = response.substr(1);
        if(next.size())
          socket.send(std::string("C" + next), ZMQ_DONTWAIT);
        break;
      case 'W': //WAIT so many seconds before asking again
        settings = response.substr(1);
        wait_until = std::time(nullptr) + 1; //TODO: parse out the interval
        return true;
      case 'C': //CAMERA image data is here
        {auto destination = www_dir + "/cameras/" + uuid + "/" + next;
        if(!system(("mkdir -p $(dirname " + destination + ")").c_str())) {
          std::fstream output(destination, std::ios::out | std::ios::binary | std::ios::trunc);
          output.write(response.data() + 1, response.size() - 1);
          logging::INFO("Wrote " + destination);
        }}
        socket.send(std::string("D" + next), ZMQ_DONTWAIT);
        next = "";
        break;
      case 'E': //ERROR came in
        settings = next = "";
        wait_until = std::time(nullptr) + 1;
        break;
      default:
        logging::WARN("Unrecognized response: " + response);
        break;
    }
    return false;
  }

  void nag() {
    //are we waiting on the next files
    if(wait_until && wait_until < std::time(nullptr)) {
      socket.send(std::string("N"), ZMQ_DONTWAIT);
      wait_until = 0;
    }
  }
};

void coordinate(zmq::context_t& context, const std::string& www_dir) {
  std::unordered_map<std::string, camera_t> cameras;
  zmq::beacon_t beacon;
  beacon.broadcast(zmq::random_port());
  beacon.subscribe();
  {std::fstream file("status.js", std::ios_base::out | std::ios_base::trunc); file << "var cameras = [];";}

  try {
    while(running) {
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
          update_status = camera.second.handle_response(www_dir) || update_status;
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

      //drop updated status json
      if(update_status) {
        std::fstream file(www_dir + "/status.js", std::ios_base::out | std::ios_base::trunc);
        file << "var cameras = [" << std::endl;
        for(auto camera = cameras.cbegin(); camera != cameras.cend(); ++camera) {
          file << "{\"endpoint\":\"" << camera->first << "\",";
          file << "\"uuid\":\"" << camera->second.uuid << "\",";
          file << "\"settings\":" << camera->second.settings << "}";
          if(std::next(camera) != cameras.cend())
            file << ',';
          file << std::endl;
        }
        file << "];" << std::endl;
        file << "var photos_dir = 'cameras';";
      }
    }
  }
  catch(...) {
  }
}

struct front_end_t {
  front_end_t(const std::string& www_dir, const std::string& pass_key, const zmq::context_t& context):
    www_dir(www_dir), pass_key(pass_key), context(context) {
  }
  worker_t::result_t work(const std::list<zmq::message_t>& job, void* request_info) {
    worker_t::result_t result{false};
    try {
      //only let very specific requests through
      auto request = http_request_t::from_string(static_cast<const char*>(job.front().data()), job.front().size());
      //configure
      if(request.path == "/configure") {
        //to do this we are going to need some authorization
        auto auth = request.query.find("pass_key");
        if(pass_key.size() && (auth == request.query.cend() || !auth->second.size() || auth->second.front() != pass_key)) {
          http_response_t response(401, "Unauthorized", "Unauthorized", headers_t{CORS});
          response.from_info(*static_cast<http_request_t::info_t*>(request_info));
          result.messages = {response.to_string()};
          return result;
        }
        //and we'll need some actual stuff to send on
        auto camera = request.query.find("camera");
        auto info = request.query.find("info");
        if(camera == request.query.cend() || info == request.query.cend() || !camera->second.size() || !info->second.size()) {
          http_response_t response(400, "Bad Request", "Bad Request", headers_t{CORS});
          response.from_info(*static_cast<http_request_t::info_t*>(request_info));
          result.messages = {response.to_string()};
          return result;
        }
        //ok checks cleared send it on
        zmq::socket_t socket(context, ZMQ_REQ);
        socket.connect(camera->second.front().c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        socket.send("I" + info->second.front(), ZMQ_DONTWAIT);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        http_response_t response(200, "OK", "Configuration Sent", headers_t{CORS, JS_MIME});
        response.from_info(*static_cast<http_request_t::info_t*>(request_info));
        result.messages = {response.to_string()};
        return result;
      }

      //check the disk
      return prime_server::http::disk_result(request, *static_cast<http_request_t::info_t*>(request_info), www_dir);
    }
    catch(const std::exception& e) {
      http_response_t response(400, "Bad Request", e.what());
      response.from_info(*static_cast<http_request_t::info_t*>(request_info));
      result.messages = {response.to_string()};
    }
    return result;
  }
  std::string www_dir;
  std::string pass_key;
  zmq::context_t context;
};

int main(int argc, char** argv) {
  if(argc < 2) {
    logging::ERROR("Usage: " + std::string(argv[0]) + " server_listen_endpoint [pass_key]");
    return 1;
  }

  //server endpoint
  std::string server_endpoint = argv[1];
  if(server_endpoint.find("://") == std::string::npos) {
    logging::ERROR("Usage: " + std::string(argv[0]) + " server_listen_endpoint [pass_key]");
    return 1;
  }

  //key for making changes
  std::string pass_key = argc > 2 ? argv[2] : "";

  //change these to tcp://known.ip.address.with:port if you want to do this across machines
  zmq::context_t context;
  std::string result_endpoint = "ipc://result_endpoint";
  std::string proxy_endpoint = "ipc://proxy_endpoint";

  //a thread for coordinating with the cameras
  running = true;
  std::thread coordinator(std::bind(coordinate, std::ref(context), "./www"));
  coordinator.detach();

  //server
  std::thread server = std::thread(std::bind(&http_server_t::serve,
    http_server_t(context, server_endpoint, proxy_endpoint + "_upstream", result_endpoint, true)));

  //load balancer for parsing
  std::thread front_end_proxy(std::bind(&proxy_t::forward, proxy_t(context, proxy_endpoint + "_upstream", proxy_endpoint + "_downstream")));
  front_end_proxy.detach();

  //front end thread
  front_end_t front_end("./www", pass_key, context);
  std::thread front_end_worker(std::bind(&worker_t::work,
    worker_t(context, proxy_endpoint + "_downstream", "ipc://NO_ENDPOINT", result_endpoint,
    std::bind(&front_end_t::work, std::ref(front_end), std::placeholders::_1, std::placeholders::_2)
  )));
  front_end_worker.detach();

  //listen for SIGINT and terminate if we hear it
  std::signal(SIGINT, [](int s){ running = false; std::this_thread::sleep_for(std::chrono::seconds(1)); exit(1); });
  server.join();


  return 0;
}
