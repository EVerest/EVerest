-- InfyPower V1.13 CAN protocol dissector (Lua)
-- SPDX-License-Identifier: Apache-2.0
--
-- Install:
--   mkdir -p ~/.local/lib/wireshark/plugins
--   cp infypower_can.lua ~/.local/lib/wireshark/plugins/
-- Reload: Analyze -> Reload Lua Plugins
--
-- Display filters:
--   infypower.src == 240          (controller 0xF0)
--   infypower.dest == 0           (module 0)
--   infypower.command == 0x03
--   infypower.src == 0 && infypower.dest == 240   (module -> controller)
--   infypower.internal            (cmd 0x17 -> internal controller, default 0xF8)
--   infypower.command == 0x17 && infypower.dest == 248
--   infypower.pair contains "req" or "resp" (add custom column in Wireshark)
--
-- Optional row color: View -> Coloring Rules -> Import -> infypower_colorfilters
--
-- Request/response pairing uses FIFO order per (module, command). Reload once
-- after opening a capture so request frames also show response links.

local infypower_proto = Proto("infypower", "InfyPower V1.13 CAN")

infypower_proto.prefs.controller_addr = Pref.uint(
    "Controller address (decimal)", 240, "Default EVerest controller: 0xF0")
infypower_proto.prefs.internal_controller_addr = Pref.uint(
    "Internal controller address (decimal)", 248,
    "Destination for internal cmd 0x17 traffic (default 0xF8)")
infypower_proto.prefs.highlight_internal = Pref.bool(
    "Highlight internal cmd 0x17 frames", true,
    "Label and colorize cmd 0x17 frames to internal controller")

-- Header fields (derived from 29-bit CAN ID, not from payload bytes)
local f_err = ProtoField.uint8("infypower.error_code", "Error Code", base.HEX)
local f_dev = ProtoField.uint8("infypower.device_number", "Device Number", base.HEX)
local f_cmd = ProtoField.uint8("infypower.command", "Command", base.HEX)
local f_dst = ProtoField.uint8("infypower.dest", "Destination Address", base.HEX)
local f_src = ProtoField.uint8("infypower.src", "Source Address", base.HEX)

-- Payload fields
local f_voltage = ProtoField.float("infypower.voltage_v", "Voltage (V)")
local f_current = ProtoField.float("infypower.current_a", "Current (A)")
local f_v_ext = ProtoField.float("infypower.v_ext_v", "External Voltage (V)")
local f_i_avail = ProtoField.float("infypower.i_avail_a", "Available Current (A)")
local f_max_v = ProtoField.uint16("infypower.max_voltage_v", "Max Voltage (V)", base.DEC)
local f_min_v = ProtoField.uint16("infypower.min_voltage_v", "Min Voltage (V)", base.DEC)
local f_max_i = ProtoField.float("infypower.max_current_a", "Max Current (A)")
local f_rated_pwr = ProtoField.uint32("infypower.rated_power_w", "Rated Power (W)", base.DEC)
local f_module_count = ProtoField.uint8("infypower.module_count", "Module Count", base.DEC)
local f_group_no = ProtoField.uint8("infypower.group_no", "Group Number", base.DEC)
local f_ambient_temp = ProtoField.int8("infypower.ambient_temp_c", "Ambient Temp (C)", base.DEC)
local f_voltage_mv = ProtoField.uint32("infypower.voltage_mv", "Voltage (mV)", base.DEC)
local f_current_ma = ProtoField.uint32("infypower.current_ma", "Current (mA)", base.DEC)
local f_on = ProtoField.bool("infypower.on", "Output On")
local f_raw = ProtoField.bytes("infypower.raw", "Raw Payload")
-- Wireshark draws request/response arrows in the packet list when these are set
local f_response_in = ProtoField.framenum(
    "infypower.response_in", "Response in", base.NONE, frametype.RESPONSE)
local f_request_in = ProtoField.framenum(
    "infypower.request_in", "Request in", base.NONE, frametype.REQUEST)
local f_pair = ProtoField.string("infypower.pair", "Pairing")
local f_internal = ProtoField.bool(
    "infypower.internal", "Internal communication (cmd 0x17 to internal controller)")

infypower_proto.fields = {
    f_err, f_dev, f_cmd, f_dst, f_src,
    f_voltage, f_current, f_v_ext, f_i_avail,
    f_max_v, f_min_v, f_max_i, f_rated_pwr,
    f_module_count, f_group_no, f_ambient_temp,
    f_voltage_mv, f_current_ma, f_on, f_raw,
    f_response_in, f_request_in, f_pair, f_internal,
}

local can_id_field = Field.new("can.id")

-- Forward declarations (Listener callback must see these as locals, not globals)
local parse_can_id
local is_infypower_frame
local get_can_id

local CMD_NAMES = {
    [0x02] = "ReadModuleCount",
    [0x03] = "ReadModuleVI",
    [0x04] = "PowerModuleStatus",
    [0x0A] = "ReadModuleCapabilities",
    [0x0B] = "ReadModuleBarcode",
    [0x0C] = "ReadModuleVIAfterDiode",
    [0x17] = "InternalComm",
    [0x1A] = "SetModuleOnOff",
    [0x1C] = "SetModuleVI",
}

local function field_uint(field)
    if field == nil then
        return nil
    end
    local v = field.value
    if type(v) == "userdata" then
        return tonumber(tostring(v))
    end
    return tonumber(v)
end

get_can_id = function()
    local id_f = can_id_field()
    if id_f == nil then
        return nil
    end
    return field_uint(id_f)
end

parse_can_id = function(can_id)
    local id = bit.band(can_id, 0x1FFFFFFF)
    return bit.band(id, 0xFF),
        bit.band(bit.rshift(id, 8), 0xFF),
        bit.band(bit.rshift(id, 16), 0x3F),
        bit.band(bit.rshift(id, 22), 0x0F),
        bit.band(bit.rshift(id, 26), 0x07)
end

local function internal_controller_addr()
    return infypower_proto.prefs.internal_controller_addr
end

local function is_internal_frame(src, dst, cmd)
    return cmd == 0x17 and dst == internal_controller_addr()
end

is_infypower_frame = function(can_id)
    if can_id == nil or can_id <= 0x7FF then
        return false
    end
    local _, dst, cmd, dev = parse_can_id(can_id)
    if is_internal_frame(nil, dst, cmd) then
        return true
    end
    return (dev == 0x0A or dev == 0x0B) and CMD_NAMES[cmd] ~= nil
end

-- Request/response pairing state (filled by tap, read during dissection)
local pair_peer = {}   -- frame number -> peer frame number
local pair_role = {}   -- frame number -> "request" | "response"
local pending = {}     -- pending[queue_key][cmd] = { frame, ... }
local last_group_req = {} -- last_group_req[cmd] = frame (for group reads, device 0x0B)
local tap_retap_done = false
local pairing_retap_in_progress = false -- guard: retap_packets() calls reset()

local function controller_addr()
    return infypower_proto.prefs.controller_addr
end

local function pending_key_module(addr)
    return "mod:" .. addr
end

local function pending_key_group(addr)
    return "grp:" .. addr
end

local function pending_queue(key, cmd)
    if pending[key] == nil then
        pending[key] = {}
    end
    if pending[key][cmd] == nil then
        pending[key][cmd] = {}
    end
    return pending[key][cmd]
end

local function link_pair(req_frame, resp_frame)
    pair_peer[req_frame] = resp_frame
    pair_peer[resp_frame] = req_frame
    pair_role[req_frame] = "request"
    pair_role[resp_frame] = "response"
end

local function is_request_frame(src, dst)
    return src == controller_addr() and dst ~= controller_addr()
end

local function is_response_frame(src, dst)
    return dst == controller_addr() and src ~= controller_addr()
end

local function pair_reset()
    -- Reassign tables (avoid pairs() during Wireshark reset; also clears nested queues)
    pair_peer = {}
    pair_role = {}
    pending = {}
    last_group_req = {}
    -- retap_packets() invokes reset(); do not re-arm retap in that case
    if not pairing_retap_in_progress then
        tap_retap_done = false
    end
end

local function pair_process_frame(pinfo, can_id)
    local src, dst, cmd, dev = parse_can_id(can_id)
    if not CMD_NAMES[cmd] or is_internal_frame(src, dst, cmd) then
        return
    end

    local fnum = pinfo.number

    if is_request_frame(src, dst) then
        if dev == 0x0B then
            last_group_req[cmd] = fnum
            table.insert(pending_queue(pending_key_group(dst), cmd), fnum)
        else
            table.insert(pending_queue(pending_key_module(dst), cmd), fnum)
        end
    elseif is_response_frame(src, dst) then
        local req_frame

        local mod_queue = pending_queue(pending_key_module(src), cmd)
        if #mod_queue > 0 then
            req_frame = table.remove(mod_queue, 1)
        elseif last_group_req[cmd] ~= nil then
            req_frame = last_group_req[cmd]
        else
            -- try any pending group queue entry for this cmd
            for _, grp_queue in pairs(pending) do
                if grp_queue[cmd] and #grp_queue[cmd] > 0 then
                    req_frame = table.remove(grp_queue[cmd], 1)
                    break
                end
            end
        end

        if req_frame ~= nil then
            link_pair(req_frame, fnum)
        end
    end
end

local function add_pairing_to_tree(subtree, pinfo)
    local fnum = pinfo.number
    local peer = pair_peer[fnum]
    if peer == nil then
        return
    end

    local role = pair_role[fnum]
    if role == "request" then
        subtree:add(f_response_in, peer):set_text("Response in frame: " .. peer)
        subtree:add(f_pair, "req"):set_generated()
        pinfo.cols.info:append(string.format("  \xE2\x86\x92 #%d", peer))
    elseif role == "response" then
        subtree:add(f_request_in, peer):set_text("Request in frame: " .. peer)
        subtree:add(f_pair, "resp"):set_generated()
        pinfo.cols.info:append(string.format("  \xE2\x86\x90 #%d", peer))
    end
end

local DEVICE_NAMES = {
    [0x0A] = "SingleModule",
    [0x0B] = "GroupModule",
}

local STATUS0_BITS = {
    [0] = "output_short_current",
    [4] = "sleeping",
    [5] = "discharge_abnormal",
}

local STATUS1_BITS = {
    [0] = "dc_side_off",
    [1] = "fault_alarm",
    [2] = "protection_alarm",
    [3] = "fan_fault_alarm",
    [4] = "over_temperature_alarm",
    [5] = "output_over_voltage_alarm",
    [6] = "walk_in_enable",
    [7] = "communication_interrupt_alarm",
}

local STATUS2_BITS = {
    [0] = "power_limit_status",
    [1] = "id_repeat_alarm",
    [2] = "load_sharing_alarm",
    [3] = "input_phase_lost_alarm",
    [4] = "input_unbalanced_alarm",
    [5] = "input_low_voltage_alarm",
    [6] = "input_over_voltage_protection",
    [7] = "pfc_side_off",
}

local function be16(buf, offset)
    if buf:len() < offset + 2 then
        return 0
    end
    return buf(offset, 2):uint()
end

local function be32(buf, offset)
    if buf:len() < offset + 4 then
        return 0
    end
    return buf(offset, 4):uint()
end

local function tvb_float_be(buf, offset)
    if buf:len() < offset + 4 then
        return nil
    end
    return buf(offset, 4):float()
end

local function format_addr(addr)
    return string.format("0x%02X", addr)
end

-- Add a field parsed from CAN ID (not from tvb) so it shows in the tree and filters work
local function add_header_field(tree, field, value, label_fmt, ...)
    local ti = tree:add(field, value)
    ti:set_generated()
    if label_fmt then
        ti:set_text(string.format(label_fmt, ...))
    end
    return ti
end

local function add_status_bits(tree, title, byte_val, bit_map)
    local bits_tree = tree:add(infypower_proto, title .. string.format(" (0x%02X)", byte_val))
    bits_tree:set_generated()
    for bitpos, name in pairs(bit_map) do
        if bit.band(byte_val, bit.lshift(1, bitpos)) ~= 0 then
            local ti = bits_tree:add(infypower_proto, name .. ": set")
            ti:set_generated()
        end
    end
end

local function payload_is_read_request(buf)
    if buf:len() < 8 then
        return false
    end
    for i = 0, 7 do
        if buf(i, 1):uint() ~= 0 then
            return false
        end
    end
    return true
end

local function is_controller_to_module(src, dst)
    local ctrl = controller_addr()
    return src == ctrl and dst ~= ctrl
end

local function dissect_payload(buf, cmd, tree, src, dst)
    local payload_tree = tree:add(infypower_proto, buf(), "Payload")
    payload_tree:set_generated()

    if buf:len() == 0 then
        local ti = payload_tree:add(infypower_proto, "No payload bytes in dissector buffer")
        ti:set_generated()
        return
    end

    payload_tree:add(f_raw, buf())

    if buf:len() < 8 then
        local ti = payload_tree:add(infypower_proto,
            string.format("Short payload (%d bytes, expected 8)", buf:len()))
        ti:set_generated()
        return
    end

    if payload_is_read_request(buf) and cmd ~= 0x1A and cmd ~= 0x1C then
        if is_controller_to_module(src, dst) then
            local ti = payload_tree:add(infypower_proto, "Read request (8 zero bytes)")
            ti:set_generated()
        else
            local ti = payload_tree:add(infypower_proto, "Response with zero payload")
            ti:set_generated()
        end
        return
    end

    if cmd == 0x03 then
        local v = tvb_float_be(buf, 0)
        local c = tvb_float_be(buf, 4)
        if v ~= nil then
            payload_tree:add(f_voltage, v):set_text(string.format("Output Voltage: %.3f V", v))
        end
        if c ~= nil then
            payload_tree:add(f_current, c):set_text(string.format("Output Current: %.3f A", c))
        end
    elseif cmd == 0x0C then
        payload_tree:add(f_v_ext, be16(buf, 0) * 0.1)
            :set_text(string.format("V_ext: %.1f V", be16(buf, 0) * 0.1))
        payload_tree:add(f_i_avail, be16(buf, 2) * 0.1)
            :set_text(string.format("I_avail: %.1f A", be16(buf, 2) * 0.1))
    elseif cmd == 0x0A then
        payload_tree:add(f_max_v, be16(buf, 0))
        payload_tree:add(f_min_v, be16(buf, 2))
        payload_tree:add(f_max_i, be16(buf, 4) * 0.1)
            :set_text(string.format("Max Current: %.1f A", be16(buf, 4) * 0.1))
        payload_tree:add(f_rated_pwr, be16(buf, 6) * 10)
            :set_text(string.format("Rated Power: %d W", be16(buf, 6) * 10))
    elseif cmd == 0x02 then
        payload_tree:add(f_module_count, buf(2, 1))
            :set_text("Module Count: " .. buf(2, 1):uint())
    elseif cmd == 0x1A then
        local on = buf(0, 1):uint() == 0
        payload_tree:add(f_on, on):set_text(on and "Output: ON" or "Output: OFF")
    elseif cmd == 0x1C then
        local mv = be32(buf, 0)
        local ma = be32(buf, 4)
        payload_tree:add(f_voltage_mv, mv)
            :set_text(string.format("Voltage: %d mV (%.3f V)", mv, mv / 1000))
        payload_tree:add(f_current_ma, ma)
            :set_text(string.format("Current: %d mA (%.3f A)", ma, ma / 1000))
    elseif cmd == 0x04 then
        payload_tree:add(f_group_no, buf(2, 1))
            :set_text("Group Number: " .. buf(2, 1):uint())
        local amb = buf(4, 1):int()
        payload_tree:add(f_ambient_temp, amb)
            :set_text(string.format("Ambient Temp: %d C", amb))
        add_status_bits(payload_tree, "Module State 0", buf(7, 1):uint(), STATUS0_BITS)
        add_status_bits(payload_tree, "Module State 1", buf(6, 1):uint(), STATUS1_BITS)
        add_status_bits(payload_tree, "Module State 2", buf(5, 1):uint(), STATUS2_BITS)
    elseif cmd == 0x0B then
        local ti = payload_tree:add(infypower_proto,
            string.format("Barcode byte 0 (char): '%c' (0x%02X)", buf(0, 1):uint(), buf(0, 1):uint()))
        ti:set_generated()
        ti = payload_tree:add(f_raw, buf(1, 7))
        ti:set_text("Barcode encoding: " .. buf(1, 7):bytes():tohex())
    elseif cmd == 0x17 then
        local ti = payload_tree:add(infypower_proto,
            string.format("Byte0: 0x%02X  Byte1: 0x%02X  Byte2-3: 0x%04X  Byte4-5: 0x%04X",
                buf(0, 1):uint(), buf(1, 1):uint(), be16(buf, 2), be16(buf, 4)))
        ti:set_generated()
        ti = payload_tree:add(infypower_proto, "Bytes 6-7: " .. buf(6, 2):bytes():tohex())
        ti:set_generated()
    else
        local ti = payload_tree:add(infypower_proto, "Payload not decoded for this command")
        ti:set_generated()
    end
end

local function highlight_internal_row(pinfo)
    if not infypower_proto.prefs.highlight_internal then
        return
    end
    if Color == nil then
        return
    end
    -- Light amber background in packet list (Wireshark 4.x)
    pinfo.cols.bg = Color.new(65535, 0xFFEE, 0xAA00)
    pinfo.cols.fg = Color.new(0, 0, 0)
end

local function dissect_infypower(buf, pinfo, tree, can_id)
    local src, dst, cmd, dev, err = parse_can_id(can_id)
    local internal = is_internal_frame(src, dst, cmd)

    if internal then
        pinfo.cols.protocol:set("InfyPower-INT")
        highlight_internal_row(pinfo)
    else
        pinfo.cols.protocol:set("InfyPower")
    end
    pinfo.cols.src:set(format_addr(src))
    pinfo.cols.dst:set(format_addr(dst))
    local subtree = tree:add(infypower_proto, buf(),
        internal and "InfyPower V1.13 (internal)" or "InfyPower V1.13")

    add_header_field(subtree, f_src, src, "Source Address: 0x%02X (%d)", src, src)
    add_header_field(subtree, f_dst, dst, "Destination Address: 0x%02X (%d)", dst, dst)
    add_header_field(subtree, f_cmd, cmd, "Command: 0x%02X (%s)", cmd, CMD_NAMES[cmd] or "unknown")
    add_header_field(subtree, f_dev, dev, "Device Number: 0x%X (%s)", dev, DEVICE_NAMES[dev] or "unknown")
    add_header_field(subtree, f_err, err, "Error Code: 0x%X", err)
    if internal then
        subtree:add(f_internal, true):set_text("Internal communication: cmd 0x17 -> internal controller")
    end

    local info_prefix = internal and "[INTERNAL 0x17] " or ""
    pinfo.cols.info:set(string.format("%s%s %02X->%02X cmd=0x%02X",
        info_prefix, CMD_NAMES[cmd] or "Infy", src, dst, cmd))

    dissect_payload(buf, cmd, subtree, src, dst)
    add_pairing_to_tree(subtree, pinfo)

    return buf:len()
end

function infypower_proto.dissector(buf, pinfo, tree)
    local can_id = get_can_id()
    if not is_infypower_frame(can_id) then
        return 0
    end
    return dissect_infypower(buf, pinfo, tree, can_id)
end

local function infypower_heuristic(buf, pinfo, tree)
    local can_id = get_can_id()
    if not is_infypower_frame(can_id) then
        return false
    end
    dissect_infypower(buf, pinfo, tree, can_id)
    return true
end

infypower_proto:register_heuristic("can", infypower_heuristic)

-- Pass 1: walk frames in order and build request/response pairs (FIFO per module+cmd)
local infy_pair_tap = Listener.new(nil, nil)

function infy_pair_tap.packet(pinfo, tvb)
    local can_id = get_can_id()
    if is_infypower_frame(can_id) then
        pair_process_frame(pinfo, can_id)
    end
end

function infy_pair_tap.reset()
    pair_reset()
end

function infy_pair_tap.draw()
    -- Pass 2: redissect so request frames also get Response-in links
    if tap_retap_done or retap_packets == nil then
        return
    end
    tap_retap_done = true
    pairing_retap_in_progress = true
    retap_packets()
    pairing_retap_in_progress = false
end
