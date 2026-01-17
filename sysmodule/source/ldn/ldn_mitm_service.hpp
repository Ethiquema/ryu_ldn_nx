/**
 * @file ldn_mitm_service.hpp
 * @brief LDN MITM Service - Main service class for ldn:u interception
 *
 * This service intercepts calls to the Nintendo ldn:u service and redirects
 * them to our RyuLdn server implementation instead of local wireless.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "interfaces/iservice.hpp"
#include "ldn_icommunication.hpp"

namespace ams::mitm::ldn {

/**
 * @brief LDN MITM Service implementation
 *
 * This class implements the ldn:u MITM service. When a game opens ldn:u,
 * this service intercepts the calls and creates our custom ICommunicationService
 * instead of the original Nintendo service.
 */
class LdnMitMService : public sf::MitmServiceImplBase {
private:
    ncm::ProgramId m_program_id;  ///< Program ID of the client process
    u64 m_client_pid;             ///< Process ID of the client (for BSD MITM tracking)

public:
    /**
     * @brief Constructor
     *
     * @param s Shared pointer to the original service
     * @param c MITM process info for the client
     */
    LdnMitMService(std::shared_ptr<::Service>&& s, const sm::MitmProcessInfo& c);

    /**
     * @brief Destructor
     */
    ~LdnMitMService();

    /**
     * @brief Determine if we should MITM this process
     *
     * This is called by Atmosphere to determine if we should intercept
     * calls from a specific process. We always return true to intercept
     * all LDN calls.
     *
     * @param client_info Process information for the client
     * @return true Always intercept
     */
    static bool ShouldMitm(const sm::MitmProcessInfo& client_info);

public:
    /**
     * @brief Create the communication service
     *
     * This is the main entry point for games. When they call
     * CreateUserLocalCommunicationService, we create our custom
     * ICommunicationService that communicates with the RyuLdn server.
     *
     * @param out Output pointer for the created service
     * @return Result code
     */
    Result CreateUserLocalCommunicationService(
        sf::Out<sf::SharedPointer<ICommunicationInterface>> out);
};

// Verify interface compliance
static_assert(ams::mitm::ldn::IsILdnMitMService<LdnMitMService>);

} // namespace ams::mitm::ldn
