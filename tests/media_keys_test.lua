-- The media-key script (scripts/mpv-media-keys.lua) under a stand-in mp:
-- the volume keys move the volume and draw the bar, and a remote's channel
-- keys step through a playlist, round its end, doing nothing with one entry.
local script = assert(arg[1], "path to mpv-media-keys.lua required")
local bindings, keys, commands, messages = {}, {}, {}, {}
local numbers = { volume = 50, ["volume-max"] = 100, ["playlist-count"] = 1, ["playlist-pos"] = 0 }
local set = {}
local overlays = 0
local ass = {}
function ass:new_event() end
function ass:pos() end
function ass:draw_start() end
function ass:rect_cw() end
function ass:draw_stop() end
function ass:append(s) self.text = self.text .. s end
package.preload['mp.assdraw'] = function()
    return { ass_new = function() return setmetatable({text=""}, {__index=ass}) end }
end
mp = {
    get_property_number = function(name, default) return numbers[name] or default end,
    get_property_bool = function(_, default) return default end,
    set_property_number = function(name, value) set[#set+1] = { name, value }; numbers[name] = value end,
    get_osd_size = function() return 640, 480 end,
    command = function(command) commands[#commands+1] = command end,
    commandv = function(_, name) messages[#messages+1] = name end,
    register_script_message = function() end,
    add_forced_key_binding = function(key, name, fn) bindings[name] = fn; keys[name] = key end,
    create_osd_overlay = function()
        return { update = function() overlays = overlays + 1 end, remove = function() end }
    end,
    add_timeout = function() return { kill = function() end } end,
}
dofile(script)

bindings['mk-vol-up']()
assert(commands[#commands] == 'no-osd add volume 5', 'volume up adds 5')
assert(overlays == 1, 'the volume bar is drawn')
assert(messages[#messages] == 'osdos-osd-menu-hide', 'the deck menu stands down for the bar')

-- The channel keys, by mpv's names for them (the players send these).
assert(keys['mk-channel-up'] == 'CHANNEL_UP' and keys['mk-channel-down'] == 'CHANNEL_DOWN', 'bound to the channel keys')

-- One entry: the channel keys do nothing.
bindings['mk-channel-up']()
bindings['mk-channel-down']()
assert(#set == 0, 'a lone entry is not stepped from')

-- Four entries: up and down, round both ends.
numbers['playlist-count'] = 4
numbers['playlist-pos'] = 2
bindings['mk-channel-up']()
assert(numbers['playlist-pos'] == 3, 'the next entry')
bindings['mk-channel-up']()
assert(numbers['playlist-pos'] == 0, 'round the end to the first')
bindings['mk-channel-down']()
assert(numbers['playlist-pos'] == 3, 'round the start to the last')
bindings['mk-channel-down']()
assert(numbers['playlist-pos'] == 2, 'the previous entry')
for _, change in ipairs(set) do
    assert(change[1] == 'playlist-pos', 'only the playlist position is set')
end
print('media keys: volume bar, channel keys through a playlist round its ends')
