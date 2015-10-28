#include <prime_server/zmq_helpers.hpp>
#include <prime_server/logging.hpp>

#include <unordered_map>
#include <vector>
#include <thread>
#include <random>

void coordinate() {
  zmq::context_t context;
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
            logging::INFO(service.first + " told me: ");
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

//TODO: spin up an http server to let a front end get info from us
void serve() {

}

int main(int argc, char** argv) {

  //TODO: make a thread for this
  coordinate();

  return 0;
}
