-- vo=drm/gpu on KMS only repaints the primary plane when the video output
-- reconfigures (a change in size or pixel format). A playlist of same-size still
-- images therefore freezes on the first frame — the playlist clock advances but no
-- KMS page-flip is issued for the next still, so the previous frame keeps scanning
-- out. On each playlist advance, nudge a render-affecting but visually negligible
-- property (video-zoom a hair up, then back to what it was) to wake the VO and
-- force a flip. The zoom is read as the nudge is made, so one set in mpv.conf
-- (a CRT's overscan) is kept rather than replaced with 0, and a file loaded
-- late (a large photo) can't leave the nudge in.
-- Loaded only for image content, so video playback is unaffected.
local base = 0

local function nudge()
    base = mp.get_property_number("video-zoom", 0) or 0
    mp.set_property_number("video-zoom", base + 0.001)
end
local function settle()
    mp.set_property_number("video-zoom", base)
end

mp.observe_property("playlist-pos", "number", function(_, pos)
    if pos == nil then return end
    mp.add_timeout(0.10, nudge)    -- two kicks: a hair up, then back,
    mp.add_timeout(0.25, settle)   -- ending clean with the new image at the zoom set
end)
