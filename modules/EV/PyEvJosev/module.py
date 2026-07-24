# SPDX-License-Identifier: Apache-2.0
# Copyright 2020 - 2023 Pionix GmbH and Contributors to EVerest
import asyncio
import sys
from pathlib import Path
import threading
import math

from everest.framework import Module, RuntimeSession, log

# fmt: off
JOSEV_WORK_DIR = Path(__file__).parent / '../../3rd_party/josev'
sys.path.append(JOSEV_WORK_DIR.as_posix())

from iso15118.evcc import EVCCHandler
from iso15118.evcc.controller.simulator import SimEVController
from iso15118.evcc.evcc_config import EVCCConfig
from iso15118.evcc.everest import context as JOSEV_CONTEXT
from iso15118.shared.exceptions import MACAddressNotFound
from iso15118.shared.exificient_exi_codec import ExificientEXICodec
from iso15118.shared.messages.enums import Protocol
from iso15118.shared.network import get_nic_mac_address
from iso15118.shared.settings import set_PKI_PATH, enable_tls_1_3

from utilities import (
    setup_everest_logging,
    determine_network_interface,
    normalize_evcc_id,
    patch_josev_config
)

setup_everest_logging()

EVEREST_CERTS_SUB_DIR = 'certs'

# The EVCCID is a MAC address only for these two protocols. ISO 15118-20 announces a manufacturer string
# instead, so an override expressed as a MAC address is meaningless there and is left alone.
MAC_ADDRESS_PROTOCOLS = (Protocol.ISO_15118_2, Protocol.DIN_SPEC_70121)


class SimEVControllerWithEvccIdOverride(SimEVController):
    """
    A SimEVController that can announce an EVCCID other than the MAC address of the HLC network interface.

    josev derives the EVCCID from the NIC, which is fixed for the lifetime of the process - in a container it is
    fixed for the lifetime of the container. That makes it impossible to simulate a second vehicle arriving,
    which matters because Autocharge identifies a vehicle purely by this address. The override is read fresh on
    every session, so changing it between sessions is enough to impersonate a different car.
    """

    def __init__(self, evcc_config: EVCCConfig, evcc_id_source):
        super().__init__(evcc_config)
        self._evcc_id_source = evcc_id_source

    async def get_evcc_id(self, protocol: Protocol, iface: str) -> str:
        """Overrides SimEVController.get_evcc_id()."""

        override = self._evcc_id_source()
        if override and protocol in MAC_ADDRESS_PROTOCOLS:
            log.info(f"Announcing EVCCID {override} instead of the {iface} MAC address")
            return override

        return await super().get_evcc_id(protocol, iface)


async def evcc_handler_main_loop(module_config: dict, exi_codec: ExificientEXICodec, evcc_id_source):
    """
    Entrypoint function that starts the ISO 15118 code running on
    the EVCC (EV Communication Controller)
    """
    iface = determine_network_interface(module_config['device'])

    evcc_config = EVCCConfig()
    patch_josev_config(evcc_config, module_config)

    await EVCCHandler(
        evcc_config=evcc_config,
        iface=iface,
        exi_codec=exi_codec,
        ev_controller=SimEVControllerWithEvccIdOverride(evcc_config, evcc_id_source),
    ).start()

class PyEVJosevModule():
    def __init__(self) -> None:
        self._es = JOSEV_CONTEXT.ev_state
        self._session = RuntimeSession()
        m = Module(self._session)

        log.update_process_name(m.info.id)

        self._setup = m.say_hello()

        etc_certs_path = m.info.paths.etc / EVEREST_CERTS_SUB_DIR
        set_PKI_PATH(str(etc_certs_path.resolve()))

        if self._setup.configs.module['enable_tls_1_3']:
            enable_tls_1_3()

        self._es.internet_service_needed = self._setup.configs.module['is_internet_service_needed']
        self._es.all_service_details = self._setup.configs.module['request_all_service_details']
        self._es.all_vas_services = self._setup.configs.module['select_all_vas_services']

        # Read by the EV controller on every session, and written by _handler_set_evcc_id from the framework
        # thread. A plain string assignment is atomic under the GIL, so no lock is needed.
        configured_evcc_id = self._setup.configs.module['evcc_id']
        self._evcc_id_override = normalize_evcc_id(configured_evcc_id)
        if configured_evcc_id and not self._evcc_id_override:
            log.error(f'Ignoring evcc_id config "{configured_evcc_id}": not a MAC address')

        # setup publishing callback
        def publish_callback(variable_name: str, value: any):
            m.publish_variable('ev', variable_name, value)

        # set publish callback for context
        JOSEV_CONTEXT.set_publish_callback(publish_callback)

        # setup handlers
        for cmd in m.implementations['ev'].commands:
            m.implement_command(
                'ev', cmd, getattr(self, f'_handler_{cmd}'))

        # init ready event
        self._ready_event = threading.Event()

        self._mod = m
        self._mod.init_done(self._ready)
        self._mod.shutdown_handler(self._shutdown)

    def start_evcc_handler(self):
        exi_codec = ExificientEXICodec()
        try:
            while True:
                self._ready_event.wait()
                try:
                    asyncio.run(evcc_handler_main_loop(self._setup.configs.module, exi_codec,
                                                       lambda: self._evcc_id_override))
                    self._mod.publish_variable('ev', 'v2g_session_finished', None)
                except KeyboardInterrupt:
                    log.debug("SECC program terminated manually")
                    break
                finally:
                    self._ready_event.clear()
        finally:
            exi_codec.shutdown()

    def _ready(self):
        log.debug("ready!")

    def _shutdown(self):
        log.debug("shutdown")

    # implementation handlers

    def _handler_start_charging(self, args) -> bool:

        self._es.DepartureTime = args['DepartureTime']
        self._es.EAmount_kWh = args['EAmount']
        self._es.EnergyTransferMode = args['EnergyTransferMode']

        if "payment_option" in args['SelectedPaymentOption']:
            self._es.PaymentOption = args['SelectedPaymentOption']['payment_option']
        if "enforce_payment_option" in args['SelectedPaymentOption']:
            self._es.enforce_payment_option = args['SelectedPaymentOption']['enforce_payment_option']

        self._ready_event.set()

        return True

    def _handler_stop_charging(self, args):
        self._es.StopCharging = True

    def _handler_pause_charging(self, args):
        self._es.Pause = True

    def _handler_set_fault(self, args):
        pass

    def _handler_set_dc_params(self, args):
        parameters = args['EvParameters']
        self._es.dc_max_current_limit = parameters['max_current_limit']
        self._es.dc_max_power_limit = parameters['max_power_limit']
        self._es.dc_max_voltage_limit = parameters['max_voltage_limit']
        self._es.dc_energy_capacity = parameters['energy_capacity']
        self._es.dc_target_current = parameters['target_current']
        self._es.dc_target_voltage = parameters['target_voltage']

    def _handler_set_bpt_dc_params(self, args):
        parameters = args['EvBPTParameters']
        self._es.dc_discharge_max_current_limit = parameters["discharge_max_current_limit"]
        self._es.dc_discharge_max_power_limit = parameters['discharge_max_power_limit']
        self._es.dc_discharge_target_current = parameters['discharge_target_current']
        self._es.minimal_soc = parameters["discharge_minimal_soc"]

    def _handler_enable_sae_j2847_v2g_v2h(self, args):
        self._es.SAEJ2847_V2H_V2G_Active = True

    def _handler_update_soc(self, args):
        self._es.actual_soc = math.floor(args['SoC'])

    def _handler_set_evcc_id(self, args) -> str:
        requested = args['EvccId']

        if not requested.strip():
            self._evcc_id_override = ''
            log.info('EVCCID override cleared, falling back to the network interface MAC address')
            return self._interface_mac_address()

        normalized = normalize_evcc_id(requested)
        if not normalized:
            log.error(f'Ignoring set_evcc_id "{requested}": not a MAC address. Keeping the current EVCCID')
            return ''

        self._evcc_id_override = normalized
        log.info(f'EVCCID set to {normalized}, it will be announced on the next V2G session')
        return normalized

    def _interface_mac_address(self) -> str:
        iface = determine_network_interface(self._setup.configs.module['device'])
        try:
            return get_nic_mac_address(iface).replace(':', '').upper()
        except MACAddressNotFound as exc:
            log.warning(f"Couldn't determine the MAC address of {iface}: {exc}")
            return ''

py_ev_josev = PyEVJosevModule()
py_ev_josev.start_evcc_handler()

