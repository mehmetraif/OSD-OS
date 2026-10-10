-- Exercise the real OSC with Lua 5.4's strict integer formatting, including
-- letterboxed/odd-size output. No mpv window or film download is needed.
local script = assert(arg[1], "path to mpv-osc.lua required")
local bindings, messages, events, commands = {}, {}, {}, {}
local width, height, subtitle, rendered = 1280, 720, false, 0
local properties = { duration = 600, ["time-pos"] = 10 }
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
package.preload['mp.utils'] = function() return {} end
mp = {
    get_opt = function() return nil end,
    get_property = function(_, default) return default end,
    get_property_number = function(name, default) return properties[name] or default end,
    get_property_bool = function(_, default) return default end,
    get_property_native = function() return subtitle and {{type="sub"}} or {} end,
    observe_property = function() end,
    register_event = function(name, fn) events[name] = fn end,
    register_script_message = function(name, fn) messages[name] = fn end,
    add_forced_key_binding = function(_, name, fn) bindings[name] = fn end,
    add_key_binding = function(_, name, fn) bindings[name] = fn end,
    remove_key_binding = function(name) bindings[name] = nil end,
    get_osd_size = function() return width, height end,
    set_osd_ass = function(w, h, text)
        if w > 0 then assert(#text > 0); rendered = rendered + 1 end
    end,
    command = function(command) commands[#commands+1] = command end,
    commandv = function() end,
    set_property_bool = function() end,
    set_property_number = function() end,
}
local function timer() return { kill=function() end, stop=function() end } end
mp.add_timeout = timer
mp.add_periodic_timer = timer
dofile(script)
for _, size in ipairs({{640,480}, {1280,720}, {1280,536}, {1920,1080}, {853,481}}) do
    width, height = size[1], size[2]
    for _, subs in ipairs({false, true}) do
        subtitle = subs
        events['start-file']()
        bindings.open_menu_up()
        assert(bindings['menu-up'], 'menu bindings were not installed')
        bindings['menu-up']()
        bindings['menu-right']()
        assert(commands[#commands] == 'seek 10')
        bindings['menu-left']()
        assert(commands[#commands] == 'seek -10')
        bindings['menu-esc']()
        assert(not bindings['menu-right'], 'closed menu retained controls')
    end
end
assert(rendered >= 40)
print('OSC: menu, seek and repeated files passed at five output sizes, with/without subtitles')
