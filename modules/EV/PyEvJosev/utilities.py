# SPDX-License-Identifier: Apache-2.0
# Copyright 2020 - 2023 Pionix GmbH and Contributors to EVerest
import logging
import netifaces

from everest.framework import log

from iso15118.evcc.evcc_config import EVCCConfig
from iso15118.shared.utils import load_requested_protocols, load_requested_energy_services

class EverestPyLoggingHandler(logging.Handler):

    def __init__(self):
        logging.Handler.__init__(self)

    def emit(self, record):
        msg = self.format(record)

        log_level: int = record.levelno
        if log_level == logging.CRITICAL:
            log.critical(msg)
        elif log_level == logging.ERROR:
            log.error(msg)
        elif log_level == logging.WARNING:
            log.warning(msg)
        # FIXME (aw): implicitely pipe everything with loglevel INFO into DEBUG
        else:
            log.debug(msg)


def setup_everest_logging():
    # remove all logging handler so that we'll have only our custom one
    # FIXME (aw): this is probably bad practice because if everyone does that, only the last one might survive
    logging.getLogger().handlers.clear()

    handler = EverestPyLoggingHandler()

    # NOTE (aw): the default formatting should be fine
    # formatter = logging.Formatter("%(levelname)s - %(name)s (%(lineno)d): %(message)s")
    # handler.setFormatter(formatter)

    logging.getLogger().addHandler(handler)


def choose_first_ipv6_local() -> str:
    for iface in netifaces.interfaces():
        if netifaces.AF_INET6 in netifaces.ifaddresses(iface):
            for netif_inet6 in netifaces.ifaddresses(iface)[netifaces.AF_INET6]:
                if 'fe80' in netif_inet6['addr']:
                    return iface

    log.warning('No necessary IPv6 link-local address was found!')
    return 'eth0'


def determine_network_interface(preferred_interface: str) -> str:
    if preferred_interface == "auto":
        return choose_first_ipv6_local()
    elif preferred_interface not in netifaces.interfaces():
        log.warning(
            f"The network interface {preferred_interface} was not found!")

    return preferred_interface


MAC_ADDRESS_HEX_DIGITS = 12
EVCC_ID_SEPARATORS = (':', '-', '.', ' ')
VID_PREFIX = 'VID:'


def normalize_evcc_id(value: str) -> str:
    """
    Turn a MAC address written in any of the forms a user is likely to type into the 12 uppercase hex digits
    that DIN 70121 and ISO 15118-2 expect in SessionSetupReq. Accepts "VID:0242AC110099", "02:42:AC:11:00:99",
    "02-42-ac-11-00-99" and "0242ac110099" alike, and returns an empty string for anything that is not a MAC
    address.

    The "VID:" prefix belongs to the OCPP token, not to the EVCCID - EvseManager prepends it again on the
    charging station side - so it is accepted for convenience and then stripped.
    """
    candidate = value.strip()

    if candidate.upper().startswith(VID_PREFIX):
        candidate = candidate[len(VID_PREFIX):]

    for separator in EVCC_ID_SEPARATORS:
        candidate = candidate.replace(separator, '')

    if len(candidate) != MAC_ADDRESS_HEX_DIGITS:
        return ''

    try:
        int(candidate, 16)
    except ValueError:
        return ''

    return candidate.upper()


def patch_josev_config(josev_config: EVCCConfig, everest_config: dict) -> None:

    josev_config.use_tls = everest_config['tls_active']

    josev_config.enforce_tls = everest_config['enforce_tls']

    josev_config.is_cert_install_needed = everest_config['is_cert_install_needed']

    josev_config.sdp_retry_cycles = 1

    protocols = [
        "DIN_SPEC_70121",
        "ISO_15118_2",
        "ISO_15118_20_AC",
        "ISO_15118_20_DC",
    ]

    if not everest_config['supported_DIN70121']:
        protocols.remove('DIN_SPEC_70121')

    if not everest_config['supported_ISO15118_2']:
        protocols.remove('ISO_15118_2')

    if not everest_config['supported_ISO15118_20_AC']:
        protocols.remove('ISO_15118_20_AC')

    if not everest_config['supported_ISO15118_20_DC']:
        protocols.remove('ISO_15118_20_DC')

    if not protocols:
        log.error("The supporting hlc protocols were not specified")

    josev_config.supported_protocols = load_requested_protocols(protocols)

    if everest_config['supported_d20_energy_services']:
        josev_config.supported_energy_services = load_requested_energy_services(
            everest_config['supported_d20_energy_services'].split(',')
        )
    else:
        josev_config.supported_energy_services = load_requested_energy_services(
             ['DC']
        )
