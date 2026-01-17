-- Auto-generated Teal module from instrument JSON (vendor=Acme model=OSC1000 id=SCOPE1)

-- Helper numeric utilities for precision handling
local function _log10(x: number): number
  -- math.log10 might not be available; use change-of-base
  return math.log(x) / math.log(10)
end

local function _round_to_sig(x: number, n: number): number
  if x == 0 then return 0 end
  local d = math.floor(_log10(math.abs(x)))
  local scale = 10 ^ (d - n + 1)
  return math.floor((x / scale) + 0.5) * scale
end

local function _round_to_multiple(x: number, step: number): number
  if step == 0 then return x end
  return math.floor((x / step) + 0.5) * step
end

local function _int_keep_significant(v: number, n: number): number
  if v == 0 then return 0 end
  local neg = v < 0
  local a = math.abs(v)
  local digits = math.floor(_log10(a)) + 1
  if digits <= n then return v end
  local factor = 10 ^ (digits - n)
  local res = math.floor((a / factor) + 0.5) * factor
  if neg then res = -res end
  return res
end

local AcmeOSC1000SCOPE1 = {}

--- Set the sample rate for a channel
--- @param id string Instrument instance identifier
--- @param channel number Channel index (1-based)
--- @param sample_rate number Sample rate in Hz
--- @return number sample_rate
function AcmeOSC1000SCOPE1:setSampleRate(id: string, channel: number, sample_rate: number): number
  local _old_channel = channel
  if channel < 1 then
    channel = 1
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  elseif channel > 2 then
    channel = 2
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  channel = math.floor(channel)

  local _old_sample_rate = sample_rate
  if sample_rate < 1 then
    sample_rate = 1
    context:log("Clamped sample_rate from " .. tostring(_old_sample_rate) .. " to " .. tostring(sample_rate))
  elseif sample_rate > 125000000 then
    sample_rate = 125000000
    context:log("Clamped sample_rate from " .. tostring(_old_sample_rate) .. " to " .. tostring(sample_rate))
  end
  local _pre_sample_rate = sample_rate
  sample_rate = _round_to_multiple(sample_rate, 0.1)
  if sample_rate ~= _pre_sample_rate then context:log("Rounded sample_rate to nearest multiple 0.1: " .. tostring(_pre_sample_rate) .. " -> " .. tostring(sample_rate)) end
  local _post_sample_rate = sample_rate
  if sample_rate < 1 then
    sample_rate = 1
    context:log("Clamped sample_rate after precision from " .. tostring(_post_sample_rate) .. " to " .. tostring(sample_rate))
  elseif sample_rate > 125000000 then
    sample_rate = 125000000
    context:log("Clamped sample_rate after precision from " .. tostring(_post_sample_rate) .. " to " .. tostring(sample_rate))
  end
  return context:call("{id}:{channel}.SETSAMPLERATE", sample_rate)
end

--- Set the gain code for a channel
--- @param id string Instrument instance identifier
--- @param channel number Channel index (1-based)
--- @param gain number Gain code (integer)
--- @return number gain
function AcmeOSC1000SCOPE1:setGain(id: string, channel: number, gain: number): number
  local _old_channel = channel
  if channel < 1 then
    channel = 1
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  elseif channel > 2 then
    channel = 2
    context:log("Clamped channel from " .. tostring(_old_channel) .. " to " .. tostring(channel))
  end
  channel = math.floor(channel)

  local _old_gain = gain
  if gain < 0 then
    gain = 0
    context:log("Clamped gain from " .. tostring(_old_gain) .. " to " .. tostring(gain))
  elseif gain > 1023 then
    gain = 1023
    context:log("Clamped gain from " .. tostring(_old_gain) .. " to " .. tostring(gain))
  end
  gain = math.floor(gain)
  local _pre_gain = gain
  gain = _int_keep_significant(gain, 2)
  if gain ~= _pre_gain then context:log("Applied integer significant-digit precision to gain: " .. tostring(_pre_gain) .. " -> " .. tostring(gain)) end
  local _post_gain = gain
  if gain < 0 then
    gain = 0
    context:log("Clamped gain after precision from " .. tostring(_post_gain) .. " to " .. tostring(gain))
  elseif gain > 1023 then
    gain = 1023
    context:log("Clamped gain after precision from " .. tostring(_post_gain) .. " to " .. tostring(gain))
  end
  return context:call("{id}:{channel}.SETGAIN", gain)
end

--- Set system mode
--- @param id string Instrument instance identifier
--- @param mode string System mode
--- @return string mode
function AcmeOSC1000SCOPE1:setMode(id: string, mode: string): string
  return context:call("{id}.SETMODE", mode)
end

return AcmeOSC1000SCOPE1
