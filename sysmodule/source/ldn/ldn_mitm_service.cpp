/**
 * @file ldn_mitm_service.cpp
 * @brief LDN MITM Service implementation
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#include "ldn_mitm_service.hpp"
#include "../debug/log.hpp"

namespace ams::mitm::ldn {

LdnMitMService::LdnMitMService(std::shared_ptr<::Service>&& s, const sm::MitmProcessInfo& c)
    : MitmServiceImplBase(std::forward<std::shared_ptr<::Service>>(s), c)
    , m_program_id(c.program_id)
{
    LOG_INFO("LDN MITM service created for program_id=0x%016lx", c.program_id.value);
}

bool LdnMitMService::ShouldMitm(const sm::MitmProcessInfo& client_info) {
    // We always want to intercept LDN calls
    // In the future, we could add filtering by title ID if needed
    LOG_VERBOSE("ShouldMitm called for program_id=0x%016lx", client_info.program_id.value);
    return true;
}

Result LdnMitMService::CreateUserLocalCommunicationService(
    sf::Out<sf::SharedPointer<ICommunicationInterface>> out)
{
    LOG_INFO("Creating UserLocalCommunicationService for program_id=0x%016lx", m_program_id.value);
    // Create our custom communication service with the client's program ID
    // The program_id is used to replace LocalCommunicationId=-1 with the real title ID
    auto service = sf::CreateSharedObjectEmplaced<ICommunicationInterface, ICommunicationService>(m_program_id);
    out.SetValue(std::move(service));
    R_SUCCEED();
}

} // namespace ams::mitm::ldn
