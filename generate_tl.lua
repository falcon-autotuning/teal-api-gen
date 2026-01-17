#!/usr/bin/env lua
-- generate_tl.lua
-- Generate a Teal (.tl) module from an instrument JSON (conforming to instrument_api_schema.json)
-- Usage: lua generate_tl.lua example_instrument.json > Module.tl
-- Requires: lua-cjson or dkjson
--
-- New behavior:
--  - Emits helper rounding functions at top of generated .tl module.
--  - Emits runtime clamping for numeric parameters (min/max).
--  - Applies "precision" semantics:
--      * precision integer + value float: keep N significant digits (scientific notation digits)
--      * precision float + value float: round to nearest multiple of precision
--      * precision integer + value int: keep N most significant digits and zero rest
--      * precision float + value int: log warning via context:log and ignore precision
--  - After precision transformation we re-clamp to ensure bounds are preserved; all adjustments log via context:log.
--  - Never throws; always coerces and logs.

local json = nil
local ok, cjson = pcall(require, "cjson")
if ok and cjson then
	json = {
		decode = function(s)
			return cjson.decode(s)
		end,
	}
else
	ok, dkjson = pcall(require, "dkjson")
	if ok and dkjson then
		json = {
			decode = function(s)
				local obj, pos, err = dkjson.decode(s, 1, nil)
				if err then
					error(err)
				end
				return obj
			end,
		}
	end
end

if not json then
	io.stderr:write("Error: requires either 'cjson' or 'dkjson' Lua JSON library.\n")
	os.exit(1)
end

local function read_file(path)
	local f, err = io.open(path, "rb")
	if not f then
		error("Failed to open file: " .. tostring(err))
	end
	local content = f:read("*a")
	f:close()
	return content
end

local function sanitize_module_name(s)
	if not s then
		return "InstrumentAPI"
	end
	return (s:gsub("[^0-9A-Za-z]", ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function camel_from_cmd(cmd)
	local parts = {}
	for part in (cmd:gsub("^_", ""):gsub("_+", "_")):gmatch("[^_]+") do
		parts[#parts + 1] = part:lower()
	end
	if #parts == 0 then
		return cmd:lower()
	end
	local out = parts[1]
	for i = 2, #parts do
		out = out .. (parts[i]:gsub("^%l", string.upper))
	end
	return out
end

local function teal_type_from_schema(t)
	if not t then
		return "any"
	end
	if t == "int" or t == "float" then
		return "number"
	end
	if t == "string" then
		return "string"
	end
	if t == "bool" then
		return "boolean"
	end
	return "any"
end

local function is_integer_value(v)
	if type(v) ~= "number" then
		return false
	end
	return math.floor(v) == v
end

local function find_io_by_name(io_list, name)
	if not io_list then
		return nil
	end
	for _, io in ipairs(io_list) do
		if io.name == name then
			return io
		end
	end
	return nil
end

local function find_output_types(outputs, channel_group, top_io, channel_groups_by_name)
	local result = {}
	if not outputs or #outputs == 0 then
		return result
	end
	if channel_group then
		local cg = channel_groups_by_name[channel_group]
		if cg and cg.io_types then
			for _, suffix in ipairs(outputs) do
				local typ = nil
				for _, iot in ipairs(cg.io_types) do
					if iot.suffix == suffix then
						typ = iot.type
						break
					end
				end
				table.insert(result, { name = suffix, type = typ })
			end
			return result
		end
	end
	for _, out in ipairs(outputs) do
		local io_entry = find_io_by_name(top_io or {}, out)
		local typ = io_entry and io_entry.type or nil
		table.insert(result, { name = out, type = typ })
	end
	return result
end

-- Emit helper functions (once) to the generated Teal module.
local function emit_helpers(lines)
	table.insert(lines, "-- Helper numeric utilities for precision handling")
	table.insert(lines, "local function _log10(x: number): number")
	table.insert(lines, "  -- math.log10 might not be available; use change-of-base")
	table.insert(lines, "  return math.log(x) / math.log(10)")
	table.insert(lines, "end")
	table.insert(lines, "")
	table.insert(lines, "local function _round_to_sig(x: number, n: number): number")
	table.insert(lines, "  if x == 0 then return 0 end")
	table.insert(lines, "  local d = math.floor(_log10(math.abs(x)))")
	table.insert(lines, "  local scale = 10 ^ (d - n + 1)")
	table.insert(lines, "  return math.floor((x / scale) + 0.5) * scale")
	table.insert(lines, "end")
	table.insert(lines, "")
	table.insert(lines, "local function _round_to_multiple(x: number, step: number): number")
	table.insert(lines, "  if step == 0 then return x end")
	table.insert(lines, "  return math.floor((x / step) + 0.5) * step")
	table.insert(lines, "end")
	table.insert(lines, "")
	table.insert(lines, "local function _int_keep_significant(v: number, n: number): number")
	table.insert(lines, "  if v == 0 then return 0 end")
	table.insert(lines, "  local neg = v < 0")
	table.insert(lines, "  local a = math.abs(v)")
	table.insert(lines, "  local digits = math.floor(_log10(a)) + 1")
	table.insert(lines, "  if digits <= n then return v end")
	table.insert(lines, "  local factor = 10 ^ (digits - n)")
	table.insert(lines, "  local res = math.floor((a / factor) + 0.5) * factor")
	table.insert(lines, "  if neg then res = -res end")
	table.insert(lines, "  return res")
	table.insert(lines, "end")
	table.insert(lines, "")
end

-- Emit clamp + precision code for a parameter
local function emit_clamp_and_precision(lines, pname, p_schema_type, pmin, pmax, precision, indent)
	indent = indent or "  "
	if pmin == nil and pmax == nil and precision == nil then
		return
	end
	-- Save original
	table.insert(lines, (indent .. ("local _old_%s = %s"):format(pname, pname)))
	-- Clamp to bounds if present
	if pmin ~= nil and pmax ~= nil then
		table.insert(lines, (indent .. ("if %s < %s then"):format(pname, tostring(pmin))))
		table.insert(lines, (indent .. ("  %s = %s"):format(pname, tostring(pmin))))
		table.insert(
			lines,
			(
				indent
				.. ('  context:log("Clamped %s from " .. tostring(_old_%s) .. " to " .. tostring(%s))'):format(
					pname,
					pname,
					pname
				)
			)
		)
		table.insert(lines, (indent .. ("elseif %s > %s then"):format(pname, tostring(pmax))))
		table.insert(lines, (indent .. ("  %s = %s"):format(pname, tostring(pmax))))
		table.insert(
			lines,
			(
				indent
				.. ('  context:log("Clamped %s from " .. tostring(_old_%s) .. " to " .. tostring(%s))'):format(
					pname,
					pname,
					pname
				)
			)
		)
		table.insert(lines, (indent .. "end"))
	elseif pmin ~= nil then
		table.insert(lines, (indent .. ("if %s < %s then"):format(pname, tostring(pmin))))
		table.insert(lines, (indent .. ("  %s = %s"):format(pname, tostring(pmin))))
		table.insert(
			lines,
			(
				indent
				.. ('  context:log("Clamped %s from " .. tostring(_old_%s) .. " to " .. tostring(%s))'):format(
					pname,
					pname,
					pname
				)
			)
		)
		table.insert(lines, (indent .. "end"))
	elseif pmax ~= nil then
		table.insert(lines, (indent .. ("if %s > %s then"):format(pname, tostring(pmax))))
		table.insert(lines, (indent .. ("  %s = %s"):format(pname, tostring(pmax))))
		table.insert(
			lines,
			(
				indent
				.. ('  context:log("Clamped %s from " .. tostring(_old_%s) .. " to " .. tostring(%s))'):format(
					pname,
					pname,
					pname
				)
			)
		)
		table.insert(lines, (indent .. "end"))
	end

	-- If it's an integer schema type, ensure integerness before applying integer-specific precision rules
	local is_int_schema = (p_schema_type == "int")
	if is_int_schema then
		table.insert(lines, (indent .. ("%s = math.floor(%s)"):format(pname, pname)))
	end

	-- Apply precision if present
	if precision ~= nil then
		-- decide if precision is integer-like
		local prec_is_int = false
		if type(precision) == "number" and math.floor(precision) == precision then
			prec_is_int = true
		end

		if p_schema_type == "float" then
			if prec_is_int then
				-- precision is integer, value is float -> keep N significant digits
				table.insert(lines, (indent .. ("local _pre_%s = %s"):format(pname, pname)))
				table.insert(lines, (indent .. ("%s = _round_to_sig(%s, %d)"):format(pname, pname, precision)))
				table.insert(
					lines,
					(
						indent
						.. ('if %s ~= _pre_%s then context:log("Applied significant-digit precision to %s: " .. tostring(_pre_%s) .. " -> " .. tostring(%s)) end'):format(
							pname,
							pname,
							pname,
							pname,
							pname
						)
					)
				)
			else
				-- precision is float -> round to nearest multiple of precision
				table.insert(lines, (indent .. ("local _pre_%s = %s"):format(pname, pname)))
				table.insert(
					lines,
					(indent .. ("%s = _round_to_multiple(%s, %s)"):format(pname, pname, tostring(precision)))
				)
				table.insert(
					lines,
					(
						indent
						.. ('if %s ~= _pre_%s then context:log("Rounded %s to nearest multiple %s: " .. tostring(_pre_%s) .. " -> " .. tostring(%s)) end'):format(
							pname,
							pname,
							pname,
							tostring(precision),
							pname,
							pname
						)
					)
				)
			end
		elseif p_schema_type == "int" then
			if prec_is_int then
				-- keep most important digits (N) and zero the rest
				table.insert(lines, (indent .. ("local _pre_%s = %s"):format(pname, pname)))
				table.insert(lines, (indent .. ("%s = _int_keep_significant(%s, %d)"):format(pname, pname, precision)))
				table.insert(
					lines,
					(
						indent
						.. ('if %s ~= _pre_%s then context:log("Applied integer significant-digit precision to %s: " .. tostring(_pre_%s) .. " -> " .. tostring(%s)) end'):format(
							pname,
							pname,
							pname,
							pname,
							pname
						)
					)
				)
			else
				-- precision float applied to int -> warn and ignore
				table.insert(
					lines,
					(
						indent
						.. ('context:log("Warning: precision %s for integer parameter %s is fractional - ignoring precision")'):format(
							tostring(precision),
							pname
						)
					)
				)
			end
		else
			-- unknown schema type (maybe missing); just ignore precision but warn
			table.insert(
				lines,
				(
					indent
					.. ('context:log("Warning: precision specified for %s but parameter type unknown - ignoring precision")'):format(
						pname
					)
				)
			)
		end

		-- Ensure still in bounds after precision; if outside, clamp and log
		if pmin ~= nil or pmax ~= nil then
			table.insert(lines, (indent .. ("local _post_%s = %s"):format(pname, pname)))
			if pmin ~= nil and pmax ~= nil then
				table.insert(lines, (indent .. ("if %s < %s then"):format(pname, tostring(pmin))))
				table.insert(lines, (indent .. ("  %s = %s"):format(pname, tostring(pmin))))
				table.insert(
					lines,
					(
						indent
						.. ('  context:log("Clamped %s after precision from " .. tostring(_post_%s) .. " to " .. tostring(%s))'):format(
							pname,
							pname,
							pname
						)
					)
				)
				table.insert(lines, (indent .. ("elseif %s > %s then"):format(pname, tostring(pmax))))
				table.insert(lines, (indent .. ("  %s = %s"):format(pname, tostring(pmax))))
				table.insert(
					lines,
					(
						indent
						.. ('  context:log("Clamped %s after precision from " .. tostring(_post_%s) .. " to " .. tostring(%s))'):format(
							pname,
							pname,
							pname
						)
					)
				)
				table.insert(lines, (indent .. "end"))
			elseif pmin ~= nil then
				table.insert(lines, (indent .. ("if %s < %s then"):format(pname, tostring(pmin))))
				table.insert(lines, (indent .. ("  %s = %s"):format(pname, tostring(pmin))))
				table.insert(
					lines,
					(
						indent
						.. ('  context:log("Clamped %s after precision from " .. tostring(_post_%s) .. " to " .. tostring(%s))'):format(
							pname,
							pname,
							pname
						)
					)
				)
				table.insert(lines, (indent .. "end"))
			else
				table.insert(lines, (indent .. ("if %s > %s then"):format(pname, tostring(pmax))))
				table.insert(lines, (indent .. ("  %s = %s"):format(pname, tostring(pmax))))
				table.insert(
					lines,
					(
						indent
						.. ('  context:log("Clamped %s after precision from " .. tostring(_post_%s) .. " to " .. tostring(%s))'):format(
							pname,
							pname,
							pname
						)
					)
				)
				table.insert(lines, (indent .. "end"))
			end
		end
	end
end

local function build_module(instrument)
	local inst_meta = instrument.instrument or {}
	local vendor = inst_meta.vendor or ""
	local model = inst_meta.model or ""
	local identifier = inst_meta.identifier or ""
	local module_name = sanitize_module_name(vendor .. model .. identifier)
	if module_name == "" then
		module_name = "InstrumentAPI"
	end

	local io_list = instrument.io or {}
	local cg_list = instrument.channel_groups or {}
	local channel_groups_by_name = {}
	for _, cg in ipairs(cg_list) do
		channel_groups_by_name[cg.name] = cg
	end

	local commands = instrument.commands or {}

	local lines = {}
	table.insert(
		lines,
		"-- Auto-generated Teal module from instrument JSON (vendor="
			.. vendor
			.. " model="
			.. model
			.. " id="
			.. identifier
			.. ")"
	)
	table.insert(lines, "")
	emit_helpers(lines)
	table.insert(lines, ("local %s = {}"):format(module_name))
	table.insert(lines, "")

	for cmd_key, cmd_def in pairs(commands) do
		local func_name = camel_from_cmd(cmd_key)
		local template = cmd_def.template or ""
		-- placeholders in order
		local placeholders = {}
		for name in template:gmatch("{([^}]+)}") do
			table.insert(placeholders, name)
		end

		local channel_group_name = cmd_def.channel_group
		local channel_param_name = nil
		local channel_param_def = nil
		if channel_group_name and channel_groups_by_name[channel_group_name] then
			local cg = channel_groups_by_name[channel_group_name]
			if cg and cg.channel_parameter and cg.channel_parameter.name then
				channel_param_name = cg.channel_parameter.name
				channel_param_def = cg.channel_parameter
			end
		end

		-- map param defs (by name or by io reference)
		local params_defs = {}
		for _, p in ipairs(cmd_def.parameters or {}) do
			if p.name then
				params_defs[p.name] = p
			end
			if p.io then
				params_defs[p.io] = p
			end
		end

		-- detect usage of channel: if any placeholder equals the channel_group name -> channel is used
		local uses_channel = false
		local channel_placeholder_found = nil
		if channel_group_name then
			for _, ph in ipairs(placeholders) do
				if ph == channel_group_name then
					uses_channel = true
					channel_placeholder_found = ph
					break
				end
			end
			if not uses_channel and params_defs[channel_group_name] then
				uses_channel = true
				channel_placeholder_found = channel_group_name
			end
		end

		-- Build function parameter order:
		local func_params = { "id" }
		if uses_channel then
			table.insert(func_params, channel_param_name or "channel")
		end

		local seen = {}
		seen["id"] = true
		if uses_channel then
			seen[channel_param_name] = true
		end

		for _, ph in ipairs(placeholders) do
			local pname = ph
			if uses_channel and ph == channel_placeholder_found then
				pname = channel_param_name
			end
			if not seen[pname] then
				table.insert(func_params, pname)
				seen[pname] = true
			end
		end

		for _, p in ipairs(cmd_def.parameters or {}) do
			local pname = p.name or p.io
			if pname and not seen[pname] then
				table.insert(func_params, pname)
				seen[pname] = true
			end
		end

		-- Documentation comments (Teal-friendly)
		if cmd_def.description and cmd_def.description ~= "" then
			table.insert(lines, ("--- %s"):format(cmd_def.description))
		else
			table.insert(lines, ("--- %s"):format(cmd_key))
		end

		-- param doclines and collect param type strings for signature
		local sig_parts = {}
		local doc_lines = {}

		table.insert(doc_lines, "--- @param id string Instrument instance identifier")
		table.insert(sig_parts, "id: string")

		if uses_channel then
			local chdesc = ""
			if channel_param_def and channel_param_def.description then
				chdesc = " " .. channel_param_def.description
			end
			table.insert(doc_lines, ("--- @param %s number%s"):format(channel_param_name, chdesc))
			table.insert(sig_parts, (channel_param_name .. ": number"))
		end

		-- other params
		for idx = 1, #func_params do
			local p = func_params[idx]
			if p == "id" then
				goto continue
			end
			if uses_channel and p == channel_param_name then
				goto continue
			end
			::continue::
		end
		for idx = 1, #func_params do
			local p = func_params[idx]
			if p == "id" then
				goto cont2
			end
			if uses_channel and p == channel_param_name then
				goto cont2
			end
			local pdef = params_defs[p]
			local ptype = nil
			local pdesc = ""
			if pdef then
				if pdef.type then
					ptype = teal_type_from_schema(pdef.type)
				end
				if pdef.io then
					local io_e = find_io_by_name(io_list, pdef.io)
					if io_e and io_e.type then
						ptype = teal_type_from_schema(io_e.type)
					end
				end
				pdesc = pdef.description or ""
			else
				-- maybe it's a channel io suffix; find in group io_types
				if channel_group_name and channel_groups_by_name[channel_group_name] then
					local cg = channel_groups_by_name[channel_group_name]
					for _, iot in ipairs(cg.io_types or {}) do
						if iot.suffix == p then
							ptype = teal_type_from_schema(iot.type)
							pdesc = iot.description or ""
							break
						end
					end
				end
			end
			if not ptype then
				ptype = "any"
			end
			if pdesc and pdesc ~= "" then
				table.insert(doc_lines, ("--- @param %s %s %s"):format(p, ptype, pdesc))
			else
				table.insert(doc_lines, ("--- @param %s %s"):format(p, ptype))
			end
			table.insert(sig_parts, (p .. ": " .. ptype))
			::cont2::
		end

		-- outputs -> return type(s)
		local outputs = cmd_def.outputs or {}
		local output_types = find_output_types(outputs, channel_group_name, io_list, channel_groups_by_name)
		local return_sig = "any"
		if #output_types == 0 then
			return_sig = "any"
			table.insert(doc_lines, "--- @return any")
		elseif #output_types == 1 then
			local ot = output_types[1]
			local otype = teal_type_from_schema(ot.type)
			return_sig = otype
			if ot.name and ot.name ~= "" then
				table.insert(doc_lines, ("--- @return %s %s"):format(otype, ot.name))
			else
				table.insert(doc_lines, ("--- @return %s"):format(otype))
			end
		else
			local rec_parts = {}
			for _, ot in ipairs(output_types) do
				local otype = teal_type_from_schema(ot.type)
				rec_parts[#rec_parts + 1] = (("%s: %s"):format(ot.name, otype))
			end
			return_sig = ("{%s}"):format(table.concat(rec_parts, ", "))
			table.insert(doc_lines, ("--- @return table %s"):format(return_sig))
		end

		-- append doc_lines
		for _, dl in ipairs(doc_lines) do
			table.insert(lines, dl)
		end

		-- function signature
		local sig = table.concat(sig_parts, ", ")
		table.insert(lines, ("function %s:%s(%s): %s"):format(module_name, func_name, sig, return_sig))

		-- Emit clamping + precision for channel param if used
		if uses_channel and channel_param_def then
			local pmin = channel_param_def.min
			local pmax = channel_param_def.max
			local prec = channel_param_def.precision
			local ptype = channel_param_def.type
			emit_clamp_and_precision(lines, channel_param_name, ptype, pmin, pmax, prec, "  ")
		end

		-- Emit clamping + precision for other params that have min/max or precision
		for _, p in ipairs(cmd_def.parameters or {}) do
			local pname = p.name or p.io
			if not pname then
				goto skip_param
			end
			if uses_channel and pname == channel_param_name then
				goto skip_param
			end
			local pmin = p.min
			local pmax = p.max
			local prec = p.precision
			local ptype = p.type
			-- Only apply clamp/precision if numeric type or precision present
			if ((pmin ~= nil or pmax ~= nil) and (ptype == "int" or ptype == "float")) or (prec ~= nil) then
				emit_clamp_and_precision(lines, pname, ptype, pmin, pmax, prec, "  ")
			end
			::skip_param::
		end

		-- Build command id string for context:call
		local cmd_id_str
		if uses_channel then
			cmd_id_str = ("{id}:{%s}.%s"):format(channel_param_name, cmd_key)
		else
			cmd_id_str = ("{id}.%s"):format(cmd_key)
		end

		-- Build call args: parameters excluding id and channel (they are embedded)
		local call_args = {}
		for i = 1, #func_params do
			local p = func_params[i]
			if p == "id" then
				goto skip_call
			end
			if uses_channel and p == channel_param_name then
				goto skip_call
			end
			table.insert(call_args, p)
			::skip_call::
		end
		local call_args_sig = table.concat(call_args, ", ")

		if call_args_sig ~= "" then
			table.insert(lines, ('  return context:call("%s", %s)'):format(cmd_id_str, call_args_sig))
		else
			table.insert(lines, ('  return context:call("%s")'):format(cmd_id_str))
		end

		table.insert(lines, "end")
		table.insert(lines, "")
	end

	table.insert(lines, ("return %s"):format(module_name))
	return table.concat(lines, "\n")
end

local function main()
	if #arg < 1 then
		io.stderr:write("Usage: lua generate_tl.lua <instrument.json>\n")
		os.exit(1)
	end
	local jsonfile = arg[1]
	local content = read_file(jsonfile)
	local ok, instrument = pcall(json.decode, content)
	if not ok then
		error("Failed to parse JSON: " .. tostring(instrument))
	end
	local module_text = build_module(instrument)
	io.write(module_text)
end

main()
