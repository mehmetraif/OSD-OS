-- What OSD/OS's mpv scripts draw their OSD with, written once: the deck menu
-- (mpv-osc.lua), the ambient deck menu (mpv-osc-ambient.lua), the volume bar
-- (mpv-media-keys.lua) and the screen saver (mpv-screensaver.lua). Not a
-- script of its own: each of them loads it from its own folder with
--
--     dofile((debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "osdos-osd.lua")
--
-- since mpv puts no script's folder on package.path for a script given as
-- one file (--script=<file>), and mpv's Lua (5.1, 5.2 or LuaJIT) names a
-- file's chunk "@<its path>", as dofile does in the tests.

local osd = {}

-- Colours and alphas, as ASS has them (BGR; alpha 00 opaque, FF clear).
osd.C_WHITE  = "&HFFFFFF&"
osd.C_BLACK  = "&H000000&"
osd.A_OPAQUE = "&H00&"
osd.A_TRANS  = "&HFF&"

-- The menus' and the bar's measures on an OSD ww x wh: the font size and the
-- left and right margins, the bar running between them.
function osd.metrics(ww, wh)
    local m = {}
    m.fs    = math.floor(wh * 0.0333333)
    m.lm    = math.floor(ww * 0.12)
    m.rm    = math.floor(ww * 0.88)
    m.bar_w = m.rm - m.lm
    return m
end

-- A filled rectangle with a border bs wide.
-- Uses ass:pos() (no \an tag) to match mpv's expected drawing coordinate origin.
function osd.draw_rect(ass, x, y, w, h, fc, fa, bs, bc)
    ass:new_event()
    ass:pos(x, y)
    ass:append(string.format(
        "{\\bord%d\\3c%s\\3a&H00&\\1c%s\\1a%s\\shad0}",
        bs, bc, fc, fa))
    ass:draw_start()
    ass:rect_cw(0, 0, w, h)
    ass:draw_stop()
end

-- A text label in VCR OSD Mono.
function osd.draw_text(ass, x, y, anchor, text, fs, fc, fa)
    ass:new_event()
    ass:append(string.format(
        "{\\an%d\\pos(%.2f,%.2f)\\fnVCR OSD Mono\\fs%d\\1c%s\\1a%s\\shad0\\bord0}%s",
        anchor, x, y, fs, fc, fa, text))
end

-- The four Scalings of Settings (MpvController::sessionArgs), which CROP steps
-- through live: how a 16:9 picture fills the 4:3 tube. Their values are
-- MpvController::scalingProperties()'s, the app's one place for them: this
-- table runs inside mpv, so it keeps a copy.
osd.SCALINGS = {
    { name = "LETTERBOX",  panscan = 0,    keepaspect = true  },
    { name = "14:9",       panscan = 0.43, keepaspect = true  },
    { name = "PAN & SCAN", panscan = 1,    keepaspect = true  },
    { name = "ANAMORPHIC", panscan = 0,    keepaspect = false },
}

-- The one in force, read off mpv, so CROP carries on from the Scaling the
-- video started with.
function osd.current_scaling()
    if mp.get_property_bool("keepaspect", true) == false then return 4 end
    local p = mp.get_property_number("panscan", 0) or 0
    if p > 0.7 then return 3 elseif p > 0.2 then return 2 end
    return 1
end

function osd.next_scaling()
    local s = osd.SCALINGS[osd.current_scaling() % #osd.SCALINGS + 1]
    mp.set_property_bool("keepaspect", s.keepaspect)
    mp.set_property_number("panscan", s.panscan)
end

-- A script's key bindings reach mpv's input from an idle handler, after the
-- callback that added or removed them returns. Flushed instead by the menus
-- and the screen saver, so that OSD/OS is told one is up (osdos-osd-menu /
-- osdos-screensaver shown, on which it sends back to mpv for it to take)
-- only once the keys are its, and told it is down only once they are given
-- back: a key in between went to input.conf (back ended the video instead
-- of closing the menu) or moved a menu that was gone (mpv 0.35 and later;
-- older ones apply bindings at once).
function osd.flush_keybindings()
    if mp.flush_keybindings then mp.flush_keybindings() end
end

return osd
