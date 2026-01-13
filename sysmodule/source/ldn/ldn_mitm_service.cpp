/**
 * @file ldn_mitm_service.cpp
 * @brief LDN MITM Service implementation
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_mitm_service.hpp"

namespace ams::mitm::ldn {

LdnMitMService::LdnMitMService(std::shared_ptr<::Service>&& s, const sm::MitmProcessInfo& c)
    : MitmServiceImplBase(std::forward<std::shared_ptr<::Service>>(s), c)
{
    // Log service creation
    // TODO: Add debug logging
}

bool LdnMitMService::ShouldMitm(const sm::MitmProcessInfo& client_info) {
    // We always want to intercept LDN calls
    // In the future, we could add filtering by title ID if needed
    AMS_UNUSED(client_info);
    return true;
}

Result LdnMitMService::CreateUserLocalCommunicationService(
    sf::Out<sf::SharedPointer<ICommunicationInterface>> out)
{
    // Create our custom communication service
    auto service = sf::CreateSharedObjectEmplaced<ICommunicationInterface, ICommunicationService>();
    out.SetValue(std::move(service));
    R_SUCCEED();
}

} // namespace ams::mitm::ldn
