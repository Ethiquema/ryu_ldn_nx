/**
 * @file iservice.hpp
 * @brief LDN MITM service interface definition (ldn:u)
 *
 * This file defines the IPC interface for the ldn:u service using
 * Atmosphere's sf (service framework) macros.
 *
 * @copyright Copyright (c) 2026 ryu_ldn_nx contributors
 * @license GPL-2.0-or-later
 */

#pragma once

#include <stratosphere.hpp>
#include "icommunication.hpp"

/**
 * @brief Interface definition for ILdnMitMService
 *
 * Commands:
 * - 0: CreateUserLocalCommunicationService - Creates the communication interface
 */
#define AMS_RYU_LDN_MITM_SERVICE(C, H)                                                                           \
    AMS_SF_METHOD_INFO(C, H, 0, Result, CreateUserLocalCommunicationService,                                     \
        (ams::sf::Out<ams::sf::SharedPointer<ams::mitm::ldn::ICommunicationInterface>> out), (out))

AMS_SF_DEFINE_MITM_INTERFACE(ams::mitm::ldn, ILdnMitMService, AMS_RYU_LDN_MITM_SERVICE, 0x7A5E89C2)
