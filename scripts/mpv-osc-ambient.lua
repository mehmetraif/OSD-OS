local assdraw = require 'mp.assdraw'
-- The drawing, Scalings and key flushing the OSD/OS scripts share, from this
-- file's own folder (see osdos-osd.lua for why so).
local osd = dofile((debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "osdos-osd.lua")

local menu_visible = false
local focus_btn = 1  -- index into buttons (CROP when shown, then STOP)
local update_timer = nil
local idle_timer = nil

local MENU_TIMEOUT = 5

local C_WHITE, C_BLACK = osd.C_WHITE, osd.C_BLACK
local A_OPAQUE, A_TRANS = osd.A_OPAQUE, osd.A_TRANS
local draw_rect, draw_text = osd.draw_rect, osd.draw_text
local SCALINGS, current_scaling, next_scaling = osd.SCALINGS, osd.current_scaling, osd.next_scaling

-- CROP is omitted when MpvController flags a decode path where --panscan
-- blanks the video (Pi 3 overlay path with 1080p Playback ON).
local hide_crop = mp.get_opt("hide-crop") == "1"
local buttons = {}
if not hide_crop then
    buttons[#buttons + 1] = { label = "CROP", action = next_scaling }
end
buttons[#buttons + 1] = { label = "STOP", action = function() mp.command("quit") end }

local function draw_menu()
    local ass = assdraw.ass_new()
    local ww, wh = mp.get_osd_size()
    if ww == 0 or wh == 0 then return end

    local m       = osd.metrics(ww, wh)
    local fs, lm, bar_w = m.fs, m.lm, m.bar_w
    local border  = 2
    local btn_h   = math.floor(fs * 1.5)
    local btn_gap = math.floor(bar_w * 0.025)
    local btn_y   = math.floor(wh * 0.8333333)
    local btn_w   = math.floor(bar_w * 0.090625)

    if not hide_crop then
        draw_text(ass, lm, math.floor(wh * 0.125), 4, "CROP: " .. SCALINGS[current_scaling()].name,
                  fs, C_WHITE, A_OPAQUE)
    end

    local bx = lm
    for i, btn in ipairs(buttons) do
        local sel    = (focus_btn == i)
        local fill_c = sel and C_WHITE or C_BLACK
        local fill_a = sel and A_OPAQUE or A_TRANS
        local text_c = sel and C_BLACK  or C_WHITE

        draw_rect(ass, bx, btn_y, btn_w, btn_h, fill_c, fill_a, border, C_WHITE)
        draw_text(ass, bx + btn_w / 2, btn_y + btn_h / 2, 5,
                  btn.label, fs, text_c, A_OPAQUE)
        bx = bx + btn_w + btn_gap
    end

    mp.set_osd_ass(ww, wh, ass.text)
end

-- OSD/OS is told the menu is up only once the keys are the menu's, and down
-- only once they are given back (osd.flush_keybindings).
local flush_keybindings = osd.flush_keybindings

-- The menu off the screen: its drawing, its refresh and its keys. OSD/OS is
-- told each way (osdos-osd-menu shown / hidden, as mpv-osc.lua tells it):
-- while the menu is up, back closes it here; otherwise OSD/OS acts on back
-- itself, at once.
local function hide_menu()
    menu_visible = false
    mp.set_osd_ass(0, 0, "")
    if update_timer then update_timer:stop() end
    for _, name in ipairs({ "menu-left", "menu-right", "menu-enter", "menu-esc", "menu-bs" }) do
        mp.remove_key_binding(name)
    end
    flush_keybindings()
    mp.commandv("script-message", "osdos-osd-menu", "hidden")
end

local function reset_idle_timer()
    if idle_timer then idle_timer:kill() end
    idle_timer = mp.add_timeout(MENU_TIMEOUT, function()
        if menu_visible then hide_menu() end
    end)
end

local function update_nav(action)
    reset_idle_timer()

    if action == "left" then
        focus_btn = focus_btn > 1 and focus_btn - 1 or #buttons
    elseif action == "right" then
        focus_btn = focus_btn < #buttons and focus_btn + 1 or 1
    elseif action == "enter" then
        buttons[focus_btn].action()
        return
    end

    draw_menu()
end

local function toggle_menu()
    if menu_visible then
        hide_menu()
        if idle_timer then idle_timer:kill() end
    else
        menu_visible = true

        mp.add_forced_key_binding("LEFT",  "menu-left",  function() update_nav("left")  end)
        mp.add_forced_key_binding("RIGHT", "menu-right", function() update_nav("right") end)
        mp.add_forced_key_binding("ENTER", "menu-enter", function() update_nav("enter") end)
        mp.add_forced_key_binding("ESC",   "menu-esc",   toggle_menu)
        mp.add_forced_key_binding("BS",    "menu-bs",    toggle_menu)
        flush_keybindings()
        -- Told once the keys are the menu's (above).
        mp.commandv("script-message", "osdos-osd-menu", "shown")

        draw_menu()
        update_timer = mp.add_periodic_timer(0.5, draw_menu)
        reset_idle_timer()
    end
end

mp.add_forced_key_binding("UP",   "open_menu_up",   toggle_menu)
mp.add_forced_key_binding("DOWN", "open_menu_down", toggle_menu)

mp.add_key_binding("ESC", "bg-esc", function() mp.command("quit") end)
mp.add_key_binding("BS",  "bg-bs",  function() mp.command("quit") end)
