// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/opcp/environment.hpp>

namespace opcp {

namespace {

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string join_url(const std::string& base, const std::string& path) {
    if (base.empty()) {
        return path;
    }
    const bool base_slash = base.back() == '/';
    const bool path_slash = !path.empty() && path.front() == '/';
    if (base_slash && path_slash) {
        return base.substr(0, base.size() - 1) + path;
    }
    if (!base_slash && !path_slash) {
        return base + "/" + path;
    }
    return base + path;
}

Environment hubject(const std::string& name, const std::string& region, const std::string& stage_suffix) {
    Environment env;
    env.name = name;
    env.api_base_url = "https://" + region + ".plugncharge" + stage_suffix + ".hubject.com";
    env.est_base_url = env.api_base_url;
    env.auth_url = "https://auth." + region + ".plugncharge.hubject.com/oauth/token";
    env.audience = env.api_base_url;
    // Verified against the QA stage for ISO 15118-2: no "est/" segment and no ISO version suffix. The
    // ISO 15118-20 suffix follows the OpenAPI description and is unverified.
    env.simpleenroll_path = "/.well-known/{ca}/simpleenroll{iso_opt}";
    env.simplereenroll_path = "/.well-known/{ca}/simplereenroll{iso_opt}";
    env.cacerts_path = "/{ca}/cacerts/{iso}/{alg}/";
    env.end_entities_path = "/v1/vra/cpo/endEntities";
    env.root_certs_path = "/v1/root/rootCerts";
    return env;
}

} // namespace

std::string Environment::expand(const std::string& path_template, const std::string& ca,
                                std::optional<IsoVersion> version) {
    std::string out = path_template;
    replace_all(out, "{ca}", ca);
    if (version.has_value()) {
        const auto& iso = info(*version);
        replace_all(out, "{iso}", iso.est_segment);
        replace_all(out, "{iso_opt}", *version == IsoVersion::ISO15118_2 ? "" : std::string("/") + iso.est_segment);
        replace_all(out, "{alg}", iso.cacerts_algorithm);
    }
    return out;
}

std::string Environment::simpleenroll_url(IsoVersion version) const {
    return join_url(est_base_url.empty() ? api_base_url : est_base_url, expand(simpleenroll_path, ca, version));
}

std::string Environment::simplereenroll_url(IsoVersion version) const {
    return join_url(est_base_url.empty() ? api_base_url : est_base_url, expand(simplereenroll_path, ca, version));
}

std::string Environment::cacerts_url(IsoVersion version) const {
    return join_url(est_base_url.empty() ? api_base_url : est_base_url, expand(cacerts_path, ca, version));
}

std::string Environment::end_entities_url() const {
    return join_url(api_base_url, expand(end_entities_path, ca, std::nullopt));
}

std::string Environment::root_certs_url() const {
    return join_url(api_base_url, expand(root_certs_path, ca, std::nullopt));
}

bool Environment::is_production() const {
    return name.find("-prod") != std::string::npos;
}

std::string Environment::validate_and_complete() {
    if (est_base_url.empty()) {
        est_base_url = api_base_url;
    }
    if (audience.empty()) {
        audience = api_base_url;
    }
    std::string missing;
    if (api_base_url.empty()) {
        missing += "api_base_url ";
    }
    if (auth_url.empty()) {
        missing += "auth_url ";
    }
    if (ca.empty()) {
        missing += "ca ";
    }
    return missing;
}

std::optional<Environment> Environment::preset(const std::string& name) {
    if (name == "custom") {
        Environment env;
        env.name = "custom";
        return env;
    }
    for (const std::string region : {"eu", "us"}) {
        if (name == "hubject-" + region + "-test") {
            return hubject(name, region, "-test");
        }
        if (name == "hubject-" + region + "-qa") {
            return hubject(name, region, "-qa");
        }
        if (name == "hubject-" + region + "-prod") {
            return hubject(name, region, "");
        }
    }
    return std::nullopt;
}

std::vector<std::string> Environment::preset_names() {
    return {"hubject-eu-test", "hubject-eu-qa",   "hubject-eu-prod", "hubject-us-test",
            "hubject-us-qa",   "hubject-us-prod", "custom"};
}

} // namespace opcp
