#ifndef SSF_SERVICES_USER_SERVICES_REMOTE_PORT_FORWARDING_H_
#define SSF_SERVICES_USER_SERVICES_REMOTE_PORT_FORWARDING_H_

#include <cstdint>

#include <vector>
#include <string>
#include <memory>

#include <boost/system/error_code.hpp>

#include "common/error/error.h"

#include "services/user_services/option_parser.h"

#include "services/user_services/base_user_service.h"
#include "services/admin/requests/create_service_request.h"
#include "services/admin/requests/stop_service_request.h"

#include "core/factories/service_option_factory.h"

#include "common/boost/fiber/detail/fiber_id.hpp"

#include "services/sockets_to_fibers/sockets_to_fibers.h"
#include "services/fibers_to_sockets/fibers_to_sockets.h"

namespace ssf {
namespace services {

template <typename Demux>
class RemotePortForwarding : public BaseUserService<Demux> {
 private:
  typedef boost::asio::fiber::detail::fiber_id::local_port_type local_port_type;

 public:
  static std::string GetFullParseName() { return "tcp-remote-forward,R"; }

  static std::string GetParseName() { return "tcp-remote-forward"; }

  static std::string GetValueName() {
    return "[[rem_ip]:]rem_port:dest_ip:dest_port";
  }

  static std::string GetParseDesc() {
    return "Forward TCP server [[rem_ip]:]rem_port to target dest_ip:dest_port "
           "from client";
  }

  static std::shared_ptr<RemotePortForwarding> CreateServiceOptions(
      std::string line, boost::system::error_code& ec) {
    auto forward_options = OptionParser::ParseForwardOptions(line, ec);

    if (ec) {
      ec.assign(::error::invalid_argument, ::error::get_ssf_category());
      return std::shared_ptr<RemotePortForwarding>(nullptr);
    }

    return std::shared_ptr<RemotePortForwarding>(new RemotePortForwarding(
        forward_options.from.addr, forward_options.from.port,
        forward_options.to.addr, forward_options.to.port));
  }

  static void RegisterToServiceOptionFactory() {
    ServiceOptionFactory<Demux>::RegisterUserServiceParser(
        GetParseName(), GetFullParseName(), GetValueName(), GetParseDesc(),
        &RemotePortForwarding::CreateServiceOptions);
  }

 public:
  virtual ~RemotePortForwarding() {}

  std::string GetName() override { return "tcp-remote-forward"; }

  std::vector<admin::CreateServiceRequest<Demux>> GetRemoteServiceCreateVector()
      override {
    std::vector<admin::CreateServiceRequest<Demux>> result;

    services::admin::CreateServiceRequest<Demux> r_forward(
        services::sockets_to_fibers::SocketsToFibers<Demux>::GetCreateRequest(
            remote_addr_, remote_port_, relay_fiber_port_));

    result.push_back(r_forward);

    return result;
  }

  std::vector<admin::StopServiceRequest<Demux>> GetRemoteServiceStopVector(
      Demux& demux) override {
    std::vector<admin::StopServiceRequest<Demux>> result;

    auto id = GetRemoteServiceId(demux);

    if (id) {
      result.push_back(admin::StopServiceRequest<Demux>(id));
    }

    return result;
  }

  bool StartLocalServices(Demux& demux) override {
    services::admin::CreateServiceRequest<Demux> l_forward(
        services::fibers_to_sockets::FibersToSockets<Demux>::GetCreateRequest(
            relay_fiber_port_, local_addr_, local_port_));

    auto p_service_factory =
        ServiceFactoryManager<Demux>::GetServiceFactory(&demux);
    boost::system::error_code ec;
    localServiceId_ = p_service_factory->CreateRunNewService(
        l_forward.service_id(), l_forward.parameters(), ec);
    if (ec) {
      SSF_LOG(kLogError) << "user_service[tcp-remote-forward]: "
                         << "local_service[fibers to sockets]: start failed: "
                         << ec.message();
    }
    return !ec;
  }

  uint32_t CheckRemoteServiceStatus(Demux& demux) override {
    services::admin::CreateServiceRequest<Demux> r_forward(
        services::sockets_to_fibers::SocketsToFibers<Demux>::GetCreateRequest(
            remote_addr_, remote_port_, relay_fiber_port_));
    auto p_service_factory =
        ServiceFactoryManager<Demux>::GetServiceFactory(&demux);
    auto status = p_service_factory->GetStatus(r_forward.service_id(),
                                               r_forward.parameters(),
                                               GetRemoteServiceId(demux));

    return status;
  }

  void StopLocalServices(Demux& demux) override {
    auto p_service_factory =
        ServiceFactoryManager<Demux>::GetServiceFactory(&demux);
    p_service_factory->StopService(localServiceId_);
  }

 private:
  RemotePortForwarding(const std::string& remote_addr, uint16_t remote_port,
                       const std::string& local_addr, uint16_t local_port)
      : local_port_(local_port),
        local_addr_(local_addr),
        remote_port_(remote_port),
        remote_addr_(remote_addr),
        remoteServiceId_(0),
        localServiceId_(0) {
    relay_fiber_port_ = remote_port_;
  }

  uint32_t GetRemoteServiceId(Demux& demux) {
    if (remoteServiceId_) {
      return remoteServiceId_;
    } else {
      services::admin::CreateServiceRequest<Demux> r_forward(
          services::sockets_to_fibers::SocketsToFibers<Demux>::GetCreateRequest(
              remote_addr_, remote_port_, relay_fiber_port_));

      auto p_service_factory =
          ServiceFactoryManager<Demux>::GetServiceFactory(&demux);
      auto id = p_service_factory->GetIdFromParameters(r_forward.service_id(),
                                                       r_forward.parameters());
      remoteServiceId_ = id;
      return id;
    }
  }

  uint16_t local_port_;
  std::string local_addr_;
  uint16_t remote_port_;
  std::string remote_addr_;

  local_port_type relay_fiber_port_;

  uint32_t remoteServiceId_;
  uint32_t localServiceId_;
};

}  // services
}  // ssf

#endif  // SSF_SERVICES_USER_SERVICES_REMOTE_PORT_FORWARDING_H_
