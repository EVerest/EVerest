// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <everest/opcp/types.hpp>

/// \file environment.hpp
/// Endpoint configuration of one Plug&Charge ecosystem stage (test / QA / production, EU / US).
///
/// The OPCP URI scheme is `https://{region}.{stage}.{domain}/{version}/{service}/{object}`. The real Hubject
/// deployment deviates from the published OpenAPI files in the EST paths (no `est/` segment, no ISO version
/// suffix for ISO 15118-2), so every path is a template here and presets carry the verified values.
///
/// Template placeholders: `{ca}` (sub-CA id, default "cpo"), `{iso}` ("ISO15118-2" / "ISO15118-20"),
/// `{iso_opt}` ("" for ISO 15118-2, "/ISO15118-20" otherwise), `{alg}` (cacerts curve name).
namespace opcp {

struct Environment {
    /// Preset name or "custom"
    std::string name{"custom"};

    /// OAuth2 token endpoint, e.g. https://auth.eu.plugncharge.hubject.com/oauth/token
    std::string auth_url;
    /// OAuth2 audience, for Hubject equal to the API base URL
    std::string audience;
    /// Base URL of the REST services (RCP, VRA), e.g. https://eu.plugncharge-qa.hubject.com
    std::string api_base_url;
    /// Base URL of the EST services; defaults to api_base_url
    std::string est_base_url;

    std::string ca{"cpo"};

    std::string simpleenroll_path{"/.well-known/est/{ca}/simpleenroll/{iso}"};
    std::string simplereenroll_path{"/.well-known/est/{ca}/simplereenroll/{iso}"};
    std::string cacerts_path{"/{ca}/cacerts/{iso}/{alg}/"};
    std::string end_entities_path{"/v1/vra/cpo/endEntities"};
    std::string root_certs_path{"/v1/root/rootCerts"};

    std::string simpleenroll_url(IsoVersion version) const;
    std::string simplereenroll_url(IsoVersion version) const;
    std::string cacerts_url(IsoVersion version) const;
    std::string end_entities_url() const;
    std::string root_certs_url() const;

    /// True for the productive Hubject stages; the CLI asks for confirmation before touching them
    bool is_production() const;

    /// Fills est_base_url from api_base_url when empty; returns a description of what is still missing
    /// (auth_url / api_base_url) or an empty string when usable
    std::string validate_and_complete();

    /// Known ecosystems: hubject-{eu,us}-{test,qa,prod} and "custom" (spec paths, no URLs)
    static std::optional<Environment> preset(const std::string& name);
    static std::vector<std::string> preset_names();

    /// Expands the placeholders of one path template
    static std::string expand(const std::string& path_template, const std::string& ca,
                              std::optional<IsoVersion> version);
};

} // namespace opcp
