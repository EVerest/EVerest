from __future__ import annotations

import os
from glob import glob
import json
import copy
from dataclasses import dataclass
from abc import ABC, abstractmethod
from typing import Any
from copy import deepcopy
from pathlib import Path
from typing import Union, Callable


class OCPPConfigAdjustmentStrategy(ABC):
    """ Strategy that manipulates a OCPP config when called. Cf. EverestConfigurationAdjustmentStrategy class
    """

    @abstractmethod
    def adjust_ocpp_configuration(self, config: dict) -> dict:
        """ Adjusts the provided configuration by making a (deep) copy and returning the adjusted configuration. """


class OCPPConfigAdjustmentStrategyWrapper(OCPPConfigAdjustmentStrategy):
    """ Simple OCPPConfigAdjustmentStrategy from a callback function.
    """

    def __init__(self, callback: Callable[[dict], dict]):
        self._callback = callback

    def adjust_ocpp_configuration(self, config: dict) -> dict:
        """ Adjusts the provided configuration by making a (deep) copy and returning the adjusted configuration. """
        config = deepcopy(config)
        return self._callback(config)


@dataclass(frozen=True)
class OCPP2XConfigVariableIdentifier:
    component_name: str
    variable_name: str
    variable_attribute_type: str = "Actual"


class GenericOCPP16ConfigAdjustment(OCPPConfigAdjustmentStrategy):
    """ Generic OCPPConfigAdjustmentStrategy for OCPP 1.6 that allows simple variable value adjustments.

    use e.g. via marker
    @pytest.mark.ocpp_config_adaptions(GenericOCPP16ConfigAdjustment([("Custom", "ExampleConfigurationKey", "test_value")]))
    """

    def __init__(self, adjustments: list[tuple[str, str, Any]]):
        self._adjustments = adjustments

    def adjust_ocpp_configuration(self, config: dict):
        config = copy.deepcopy(config)
        for (category, variable, value) in self._adjustments:
            config.setdefault(category, {})[variable] = value
        return config


class GenericOCPP2XConfigAdjustment(OCPPConfigAdjustmentStrategy):
    """ Generic OCPPConfigAdjustmentStrategy for OCPP 2.X that allows simple variable value adjustments.

    use e.g. via marker
    @pytest.mark.ocpp_config_adaptions(GenericOCPP2XConfigAdjustment([(OCPP2XConfigVariableIdentifier("CustomCntrlr","TestVariableName", "Actual"), "test_value")]))

    """

    def __init__(self, adjustments: list[tuple[OCPP2XConfigVariableIdentifier, Any]]):
        self._adjustments = adjustments

    def adjust_ocpp_configuration(self, config: dict):
        config = copy.deepcopy(config)
        for identifier, value in self._adjustments:
            self._set_value_in_v2_config(config, identifier, value)
        return config

    @staticmethod
    def _get_value_from_v2_config(ocpp_config: dict, identifier: OCPP2XConfigVariableIdentifier):
        for (component, schema) in ocpp_config.items():
            if component == identifier.component_name:
                attributes = schema["properties"][identifier.variable_name]["attributes"]
                for attribute in attributes:
                    if attribute["type"] == identifier.variable_attribute_type:
                        return attribute["value"]

    @staticmethod
    def _set_value_in_v2_config(ocpp_config: dict, identifier: OCPP2XConfigVariableIdentifier,
                                value: Any):
        for (component, schema) in ocpp_config.items():
            if component == identifier.component_name:
                attributes = schema["properties"][identifier.variable_name]["attributes"]
                for attribute in attributes:
                    if attribute["type"] == identifier.variable_attribute_type:
                        attribute["value"] = value


class _OCPP2XNetworkConnectionProfileAdjustment(OCPPConfigAdjustmentStrategy):
    """ Adjusts the OCPP 2.X Network Connection Profile by injecting the right host, port and chargepoint id.

    This is utilized by the `LibOCPP2XConfigurationHelper`.

    """

    def __init__(self, central_system_port: int | str = None, central_system_host: str = None, security_profile: int = None):
        self._central_system_port = central_system_port
        self._central_system_host = central_system_host
        self._security_profile = security_profile

    def adjust_ocpp_configuration(self, config: dict):
        config = deepcopy(config)
        network_connection_profiles = json.loads(GenericOCPP2XConfigAdjustment._get_value_from_v2_config(
            config, OCPP2XConfigVariableIdentifier("InternalCtrlr", "NetworkConnectionProfiles", "Actual")))
        for network_connection_profile in network_connection_profiles:
            selected_security_profile = network_connection_profile["connectionData"][
                "securityProfile"] if self._security_profile is None else self._security_profile
            selected_central_system_port = network_connection_profile["connectionData"][
                "ocppCsmsUrl"] if self._central_system_port is None else self._central_system_port
            selected_central_system_host = network_connection_profile["connectionData"][
                "ocppCsmsUrl"] if self._central_system_host is None else self._central_system_host
            protocol = "ws" if selected_security_profile == 1 else "wss"
            network_connection_profile["connectionData"][
                "ocppCsmsUrl"] = f"{protocol}://{selected_central_system_host}:{selected_central_system_port}"
            network_connection_profile["connectionData"][
                "securityProfile"] = selected_security_profile
        GenericOCPP2XConfigAdjustment._set_value_in_v2_config(config, OCPP2XConfigVariableIdentifier("InternalCtrlr", "NetworkConnectionProfiles",
                                                                                                     "Actual"), json.dumps(network_connection_profiles))
        return config


class _OCPP2XSecondaryConnectionAdjustment(OCPPConfigAdjustmentStrategy):
    """ Declares a secondary OCPP connection in the OCPP 2.X device model configuration.

    A secondary connection exists because an ``OCPPCommCtrlr`` component instance exists for it, so
    this injects that component plus a ``NetworkConnectionProfile`` for the network configuration
    slot it is given. libocpp creates the matching ``NetworkConfiguration_<slot>`` component itself
    when it migrates the profile blob, so none has to be supplied here.

    Utilized by the `LibOCPP2XConfigurationHelper`.
    """

    def __init__(self, connection_id: str, central_system_port: int | str, central_system_host: str = "127.0.0.1",
                 network_configuration_slot: int = 3, security_profile: int = 1, identity: str | None = None,
                 enabled: bool = True, upstream_msgs: str = "", downstream_msgs: str = "",
                 allow_set_variables: str = "", aligned_data_interval: int = 0,
                 aligned_data_measurands: str = "Energy.Active.Import.Register,Power.Active.Import"):
        self._connection_id = connection_id
        self._central_system_port = central_system_port
        self._central_system_host = central_system_host
        self._slot = network_configuration_slot
        self._security_profile = security_profile
        self._identity = identity
        self._enabled = enabled
        self._upstream_msgs = upstream_msgs
        self._downstream_msgs = downstream_msgs
        self._allow_set_variables = allow_set_variables
        self._aligned_data_interval = aligned_data_interval
        self._aligned_data_measurands = aligned_data_measurands

    @staticmethod
    def _variable(variable_name: str, data_type: str, mutability: str, value=None, description: str = ""):
        attribute = {"type": "Actual", "mutability": mutability}
        if value is not None:
            attribute["value"] = value
        return {
            "variable_name": variable_name,
            "characteristics": {"supportsMonitoring": True, "dataType": data_type},
            "attributes": [attribute],
            "description": description,
            "type": "string" if data_type in ("string", "MemberList", "SequenceList") else data_type,
        }

    def _ocpp_comm_ctrlr_component(self) -> dict:
        return {
            "description": f"Secondary OCPP connection {self._connection_id} (injected by everest-testing)",
            "type": "object",
            "name": "OCPPCommCtrlr",
            "instance": self._connection_id,
            "properties": {
                "OCPPCommCtrlrEnabled": self._variable("Enabled", "boolean", "ReadWrite", self._enabled),
                "NetworkConfigurationPriority": self._variable(
                    "NetworkConfigurationPriority", "SequenceList", "ReadWrite", str(self._slot)),
                "ActiveNetworkProfile": self._variable("ActiveNetworkProfile", "integer", "ReadOnly"),
                "SecurityProfile": self._variable("SecurityProfile", "integer", "ReadOnly"),
                "HeartbeatInterval": self._variable("HeartbeatInterval", "integer", "ReadWrite"),
                "DownstreamMsgs": self._variable(
                    "DownstreamMsgs", "MemberList", "ReadWrite", self._downstream_msgs),
                "UpstreamMsgs": self._variable("UpstreamMsgs", "MemberList", "ReadWrite", self._upstream_msgs),
                "AllowSetVariables": self._variable(
                    "AllowSetVariables", "string", "ReadWrite", self._allow_set_variables),
                "EnableFirmwareStatusNotificationOtherConnection": self._variable(
                    "EnableFirmwareStatusNotificationOtherConnection", "boolean", "ReadWrite", False),
            },
        }

    def _monitoring_ctrlr_component(self) -> dict:
        """ The connection's own list of monitors it wants NotifyEvents for (section 5.5).

        Without it the connection cannot be subscribed to anything and its monitors report to the
        CSMS instead, so it is injected for every secondary connection rather than on request.
        """
        return {
            "description": f"Monitor subscriptions of secondary OCPP connection {self._connection_id} "
                           "(injected by everest-testing)",
            "type": "object",
            "name": "MonitoringCtrlr",
            "instance": self._connection_id,
            "properties": {
                "SubscribedMonitors": self._variable("SubscribedMonitors", "MemberList", "ReadWrite", ""),
            },
        }

    def _aligned_data_ctrlr_component(self) -> dict:
        """ The connection's own clock-aligned meter values (section 4.5.1). """
        return {
            "description": f"Clock-aligned meter values of secondary OCPP connection {self._connection_id} "
                           "(injected by everest-testing)",
            "type": "object",
            "name": "AlignedDataCtrlr",
            "instance": self._connection_id,
            "properties": {
                "Interval": self._variable("Interval", "integer", "ReadWrite", self._aligned_data_interval),
                "Measurands": self._variable(
                    "Measurands", "MemberList", "ReadWrite", self._aligned_data_measurands),
            },
        }

    def adjust_ocpp_configuration(self, config: dict):
        config = deepcopy(config)

        # The component config dict is keyed by file stem, and every entry is written back out as a
        # file, so a new stem simply materializes as a new component config.
        config[f"OCPPCommCtrlr_{self._connection_id}"] = self._ocpp_comm_ctrlr_component()
        config[f"MonitoringCtrlr_{self._connection_id}"] = self._monitoring_ctrlr_component()
        config[f"AlignedDataCtrlr_{self._connection_id}"] = self._aligned_data_ctrlr_component()

        protocol = "ws" if self._security_profile == 1 else "wss"
        profile = {
            "configurationSlot": self._slot,
            "connectionData": {
                "messageTimeout": 30,
                "ocppCsmsUrl": f"{protocol}://{self._central_system_host}:{self._central_system_port}",
                "ocppInterface": "Wired0",
                "ocppTransport": "JSON",
                "ocppVersion": "OCPP20",
                "securityProfile": self._security_profile,
            },
        }
        if self._identity is not None:
            profile["connectionData"]["identity"] = self._identity

        profiles = json.loads(GenericOCPP2XConfigAdjustment._get_value_from_v2_config(
            config, OCPP2XConfigVariableIdentifier("InternalCtrlr", "NetworkConnectionProfiles", "Actual")))
        # Replace rather than append, so re-running against an already adjusted config is idempotent.
        profiles = [p for p in profiles if p.get("configurationSlot") != self._slot]
        profiles.append(profile)
        GenericOCPP2XConfigAdjustment._set_value_in_v2_config(
            config, OCPP2XConfigVariableIdentifier("InternalCtrlr", "NetworkConnectionProfiles", "Actual"),
            json.dumps(profiles))

        return config


class _OCPP16ComponentConfigCsmsUrlAdjustment(OCPPConfigAdjustmentStrategy):
    """ Rewrites the ``OcppCsmsUrl`` of every ``NetworkConfiguration_N`` component (device-model
    backed OCPP1.6 configuration) that carries a non-empty value or non-empty default to the test
    CSMS URL.

    The URL is written as an explicit ``Actual`` attribute value (which the device-model DB init
    treats as operator-set), using the exact same scheme-less ``{host}:{port}/{charge_point_id}``
    format ``LibOCPP16ConfigurationHelper`` writes to ``Internal/CentralSystemURI``. This keeps
    multi-slot 1.6 tests pointed at the mock CSMS.

    The component config is the ``{file_stem: component_json}`` dict (same shape as
    ``LibOCPP2XConfigurationHelper._get_config``).
    """

    def __init__(self, central_system_host: str, central_system_port: int | str, charge_point_id: str):
        self._central_system_host = central_system_host
        self._central_system_port = central_system_port
        self._charge_point_id = charge_point_id

    def adjust_ocpp_configuration(self, config: dict) -> dict:
        config = deepcopy(config)
        url = f"{self._central_system_host}:{self._central_system_port}/{self._charge_point_id}"
        for component_name, component in config.items():
            if not component_name.startswith("NetworkConfiguration"):
                continue
            ocpp_csms_url = component.get("properties", {}).get("OcppCsmsUrl")
            if not ocpp_csms_url:
                continue
            attributes = ocpp_csms_url.get("attributes", [])
            current_value = None
            for attribute in attributes:
                if attribute.get("type") == "Actual" and "value" in attribute:
                    current_value = attribute["value"]
            if current_value is None:
                current_value = ocpp_csms_url.get("default")
            if current_value in (None, ""):
                continue
            for attribute in attributes:
                if attribute.get("type") == "Actual":
                    attribute["value"] = url
        return config


class LibOCPPConfigurationHelperBase(ABC):
    """ Helper for parsing / adapting the LibOCPP configuration and dumping it a database file. """

    def generate_ocpp_config(self,
                             target_ocpp_config_path: Path,
                             target_ocpp_user_config_file: Path,
                             source_ocpp_config_path: Path,
                             central_system_host: str,
                             central_system_port: Union[str, int],
                             configuration_strategies: list[OCPPConfigAdjustmentStrategy] | None = None):
        config = self._get_config(source_ocpp_config_path)

        configuration_strategies = configuration_strategies if configuration_strategies else []

        for v in [self._get_default_strategy(central_system_port, central_system_host)] + configuration_strategies:
            config = v.adjust_ocpp_configuration(config)

        self._store_config(config, target_ocpp_config_path)
        target_ocpp_user_config_file.write_text("{}")

        return config

    @abstractmethod
    def _get_config(self, source_ocpp_config_path: Path):
        pass

    @abstractmethod
    def _get_default_strategy(self, central_system_port: int | str,
                              central_system_host: str) -> OCPPConfigAdjustmentStrategy:
        pass

    @abstractmethod
    def _store_config(self, config, target_ocpp_config_file):
        pass


class LibOCPP16ConfigurationHelper(LibOCPPConfigurationHelperBase):
    def _get_config(self, source_ocpp_config_path: Path):
        return json.loads(source_ocpp_config_path.read_text())

    def _get_default_strategy(self, central_system_port, central_system_host):
        def adjust_ocpp_configuration(config: dict) -> dict:
            config = deepcopy(config)
            charge_point_id = config["Internal"]["ChargePointId"]
            config["Internal"][
                "CentralSystemURI"] = f"{central_system_host}:{central_system_port}/{charge_point_id}"
            return config

        return OCPPConfigAdjustmentStrategyWrapper(adjust_ocpp_configuration)

    def _store_config(self, config, target_ocpp_config_file):
        with target_ocpp_config_file.open("w") as f:
            json.dump(config, f)


class LibOCPP2XConfigurationHelper(LibOCPPConfigurationHelperBase):

    def _get_config(self, source_ocpp_config_path: Path):
        config = {}
        file_list_standardized = glob(
            str(source_ocpp_config_path / "standardized" / "*.json"), recursive=False)
        file_list_custom = glob(
            str(source_ocpp_config_path / "custom" / "*.json"), recursive=False)
        file_list = file_list_standardized + file_list_custom
        for file in file_list:
            # Get component from file name
            _, tail = os.path.split(file)
            component_name, _ = os.path.splitext(tail)
            # Store json in dict
            with open(file) as f:
                config[component_name] = json.load(f)
        return config

    def _get_default_strategy(self, central_system_port: int | str,
                              central_system_host: str) -> OCPPConfigAdjustmentStrategy:
        return _OCPP2XNetworkConnectionProfileAdjustment(central_system_port, central_system_host)

    def _store_config(self, config, target_ocpp_config_path):
        # Just store all in the 'standardized' folder
        path = target_ocpp_config_path / "standardized"
        for key, value in config.items():
            file_name = path / (key + '.json')
            file_name.parent.mkdir(parents=True, exist_ok=True)
            with file_name.open("w+") as f:
                json.dump(value, f)
